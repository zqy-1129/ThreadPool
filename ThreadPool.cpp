#include "ThreadPool.h"

#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 10;

// ===============================线程池类实现==================================
// 线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(4)
    , taskSize_(0)
    , currThreadSize_(0)
    , idleThreadSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
{}

// 线程池析构
ThreadPool::~ThreadPool()
{
    // 错误版本
    // isPoolRunning_ = false;
    // notEmpty_.notify_all();
    // // 等待线程池里面所有的线程返回，阻塞和正在执行任务两种
    // std::unique_lock<std::mutex> lock(taskQueMtx_);
    // /**
    //  * 项目中出现的问题：会产生死锁，当有线程在notify_all之后运行结束并运行到
    //  * notEmpty_.wait(lock)这时候会阻塞在这里，但是notEmpty_notify_all()已经执行完了
    //  * 不会有在通知其唤醒了，因此这里形成死锁，exitCond_等待线程数释放为零，notEmpty等待通知
    //  */
    // exitCond_.wait(lock, [&]()->bool {
    //     return threads_.size() == 0;
    // });

    // 正确版本
    isPoolRunning_ = false;
    // 等待线程池里面所有的线程返回，阻塞和正在执行任务两种
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    /**
     * 项目中出现的问题：会产生死锁，当有线程在notify_all之后运行结束并运行到
     * notEmpty_.wait(lock)这时候会阻塞在这里，但是notEmpty_notify_all()已经执行完了
     * 不会有在通知其唤醒了，因此这里形成死锁，exitCond_等待线程数释放为零，notEmpty等待通知
     */
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]()->bool {
        return threads_.size() == 0;
    });
}

// 开启线程池
void ThreadPool::start(int initThreadSize) 
{   
    // 设置线程池的状态
    isPoolRunning_ = true;
    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    currThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; i++) 
    {   
        // 创建线程对象时候，将线程函数给线程对象
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(
            std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));   // 这里是14的新特性
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动所有线程
    for (auto& thread : threads_) 
    {
        thread.second->start();     // 需要一个线程函数
        idleThreadSize_++;          // 记录初始空闲线程数量
    }
}

// 设置线程池模式
void ThreadPool::setMode(PoolMode mode)
{       
    if (checkRunningState()) return;
    poolMode_ = mode;
}

// 设置任务队列上限的阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshHold)
{
    if (checkRunningState()) return;
    taskQueMaxThreshHold_ = threshHold;
}

// 设置线程池cached模式先线程的阈值
void ThreadPool::setThreadSizeThreshHold(int threshHold) 
{
    if (checkRunningState()) return;
    if (poolMode_ != PoolMode::MODE_CACHED) return;
    threadSizeThreshHold_ = threshHold;
}

// 给线程池提交任务 用户调用该接口传入任务对象
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{   
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 线程的通信 等待任务队列有空余
    // 用户提交任务 最长阻塞不能超过一秒 否则判断提交任务失败返回
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
        return taskQue_.size() < (size_t)taskQueMaxThreshHold_;  // 等待条件满足或者达到最长等待时间
    }))
    {   
        // 表示添加失败
        std::cerr << "tash queue is full, submit task fail." << std::endl;
        return Result(sp, false);
    }

    // 如果有空余 把任务放入任务队列
    taskQue_.emplace(sp);
    taskSize_++;

    // 新放了任务 任务队列不空 notEmpty_通知
    notEmpty_.notify_all();

    // 这里需要根据任务数量和空闲线程数量，判断是否需要创建新的线程出来
    // cached模式适合任务处理比较紧急，场景：小而快的任务
    if (poolMode_ == PoolMode::MODE_CACHED 
        && taskSize_ > idleThreadSize_
        && currThreadSize_ < threadSizeThreshHold_)
    {   
        // 创建新线程
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(
            std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));   // 这里是14的新特性
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        // 启动线程
        threads_[threadId]->start();
        currThreadSize_++;
        idleThreadSize_++;
        std::cout << "新线程创建成功，当前线程数量: " << currThreadSize_ << std::endl;
    }

    // 返回任务结果

    /**
     * 这里有两种设计方式
     * task->getResult();   使用任务的成员方法获得一个结果对象
     * Result(task);        使用传入任务进行结果对象构造
     * 第一个方式是不可以的，因为task在多线程中，task有可能先于使用被析构
     * 这就会导致依赖task对象的Result对象无法使用所以这种设计是不合理的
     */
    return Result(sp);
}

 // 线程执行函数 线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(int threadId)
{   
    auto lastTime = std::chrono::high_resolution_clock().now();
    // while (isPoolRunning_)    // 这样是当线程池对象析构之后，不会再执行任务，所有的任务直接退出
    for (;;)  // 这样是完成任务之后在析构
    {   
        std::shared_ptr<Task> task;
        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid: " << std::this_thread::get_id() 
                << " 尝试获取任务..." << std::endl;

            // cached模式下，有可能已经创建了很多线程，但是空闲的时间超高60s，应该将多余的线程回收
            // 超过initThreadSize_数量的线程要进行回收

            // 每一秒中返回一次 区分超时返回还是任务待执行返回
            while (taskQue_.empty())
            {       
                // 线程池要结束，回收线程资源
                if (!isPoolRunning_) 
                {
                    threads_.erase(threadId);
                    std::cout << "tid: " << std::this_thread::get_id() << " 线程池结束，回收资源\n";
                    exitCond_.notify_all();
                    return; // 线程函数结束，线程结束
                }
                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && currThreadSize_ > initThreadSize_)
                        {
                            // 开始回收当前线程
                            // 记录线程数量相关变量的值修改
                            // 将线程对象从线程列表容器中删除
                            std::cout << "tid: " << std::this_thread::get_id() << " 空闲超时回收\n";
                            currThreadSize_--;
                            idleThreadSize_--;
                            threads_.erase(threadId);
                            return; // 线程函数返回，结束当前线程
                        }
                    }       
                }
                else 
                {
                    // 等待notEmpty_条件
                    notEmpty_.wait(lock);
                }

                // // 线程池要结束，回收线程资源
                // if (!isPoolRunning_) 
                // {
                //     threads_.erase(threadId);
                //     std::cout << "tid: " << std::this_thread::get_id() << " 线程池结束，回收资源\n";
                //     exitCond_.notify_all();   // 通知线程池销毁函数回收线程资源
                //     return;
                // }
            }

            // 线程池要结束，回收线程资源
            // if (!isPoolRunning_) 
            // {
            //     break;
            // }

            idleThreadSize_--;

            std::cout << "tid: " << std::this_thread::get_id() 
                << " 获取任务成功..." << std::endl;

            // 从任务队列中取出一个任务
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 如果依然有剩余任务 继续通知其他线程执行任务
            if (taskQue_.size() > 0) notEmpty_.notify_all();

            // 取出一个任务 进行通知
            notFull_.notify_all();
        }   // 锁被释放

        // 当前线程负责执行这个任务 这里需要不再锁中
        if (task != nullptr) 
        {   
            task->exec();
        }
        lastTime = std::chrono::high_resolution_clock().now();
        idleThreadSize_++;
    }
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

// ===============================线程类实现===================================
int Thread::gernerateId_ = 0;
// 线程构造
Thread::Thread(ThreadFunc func) 
    : func_(func)
    , threadId_(gernerateId_++)
{}

// 线程析构
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
    // 创建线程执行线程函数
    std::thread t(func_, threadId_);   // 这里是14的新特性
    // 设置分离线程
    t.detach();     
}

int Thread::getId() const 
{
    return threadId_;  
}

// ===============================结果类实现===================================
Result::Result(std::shared_ptr<Task> task, bool isValid) 
    : task_(task)
    , isValid_(isValid)
{
    task->setResult(this);
}

// 用户调用
Any Result::get()
{
    if (!isValid_)
    {
        return Any();
    }

    // 这里是等待返回值
    sem_.wait();  // 这里没有执行完，这里会阻塞
    return std::move(any_);
}

void Result::setVal(Any any)
{
    // 存储task的返回值
    this->any_ = std::move(any);
    sem_.post();    // 增加信号量资源
}

// ===============================任务类实现===================================
Task::Task() : result_(nullptr)
{}

void Task::exec()
{   
    if (result_ != nullptr) 
        result_->setVal(run());  // 这里是多态调用
}

void Task::setResult(Result* res)
{   
    result_ = res;
}