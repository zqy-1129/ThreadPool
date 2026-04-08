#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <future>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 10;

// 线程池支持的类型
enum class PoolMode // 加上class可以使得我们使用枚举类型中的类型不会有类型冲突，使用作用域标识
{
    MODE_FIXED,     // 固定数量的线程
    MODE_CACHED,    // 线程数量可增长
};

// 线程
class Thread 
{
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(int)>;
    // 线程构造
    Thread(ThreadFunc func) 
        : func_(func)
        , threadId_(gernerateId_++)
    {}

    // 线程析构
    ~Thread() = default;

    // 启动线程
    void start() 
    {
        // 创建线程执行线程函数
        std::thread t(func_, threadId_);   // 这里是14的新特性
        // 设置分离线程
        t.detach();     
    }

    // 获取线程Id
    int getId() const 
    {
        return threadId_;  
    }

private:
    ThreadFunc func_;
    static int gernerateId_;
    int threadId_;
};

int Thread::gernerateId_ = 0;

// 线程池
class ThreadPool
{
public:
    ThreadPool()
        : initThreadSize_(4)
        , taskSize_(0)
        , currThreadSize_(0)
        , idleThreadSize_(0)
        , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
        , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
        , poolMode_(PoolMode::MODE_FIXED)
        , isPoolRunning_(false)
    {}
    
    ~ThreadPool() 
    {
        isPoolRunning_ = false;
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();
        exitCond_.wait(lock, [&]()->bool {
            return threads_.size() == 0;
        });
    }

    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency())
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
    void setMode(PoolMode mode)
    {
        if (checkRunningState()) return;
        poolMode_ = mode;
    }

    // 设置任务队列上限的阈值
    void setTaskQueMaxThreshHold(int threshHold)
    {
        if (checkRunningState()) return;
        taskQueMaxThreshHold_ = threshHold;
    }

    // 设置线程池cached模式先线程的阈值
    void setThreadSizeThreshHold(int threshHold)
    {
        if (checkRunningState()) return;
        threadSizeThreshHold_ = threshHold;
    }

    // 给线程池提交任务
    // 使用可变参模板编程，让用户提交任意类型的任务，任意类型的参数
    // 这里使用函数表达式使用decltype推导返回值类型，使用std::packaged_task包装任务，使用std::future获取结果
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
    {
        using ReturnType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        auto result = task->get_future();

        // 获取锁
        std::unique_lock<std::mutex> lock(taskQueMtx_);

        // 线程的通信 等待任务队列有空余
        // 用户提交任务 最长阻塞不能超过一秒 否则判断提交任务失败返回
        if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
            return taskQue_.size() < (size_t)taskQueMaxThreshHold_;  // 等待条件满足或者达到最长等待时间
        }))
        {   
            // 表示添加失败,返回一个空的任务结果对象
            std::cerr << "tash queue is full, submit task fail." << std::endl;
            auto emptyTask = std::make_shared<std::packaged_task<ReturnType()>>(
                []()->ReturnType {
                    return ReturnType();
                }
            );
            (*emptyTask)();
            return emptyTask->get_future();
        }

        // 如果有空余 把任务放入任务队列，由于我们的任务是由返回值的所以需要将task包装一下，放入任务队列中
        taskQue_.emplace([task](){  (*task)(); });
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
        return result;
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
private:
    // 线程执行函数
    void threadFunc(int threadId)
    {   
        auto lastTime = std::chrono::high_resolution_clock().now();
        // while (isPoolRunning_)    // 这样是当线程池对象析构之后，不会再执行任务，所有的任务直接退出
        for (;;)  // 这样是完成任务之后在析构
        {   
            Task task;
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
                }

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
                task();
            }
            lastTime = std::chrono::high_resolution_clock().now();
            idleThreadSize_++;
        }
    }

    // 检查运行状态
    bool checkRunningState() const
    {
        return isPoolRunning_;
    }

    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表
    size_t initThreadSize_;                                     // 初始线程数量
    std::atomic_int currThreadSize_;                            // 当前线程数
    std::atomic_int idleThreadSize_;                            // 记录空闲线程的数量
    int threadSizeThreshHold_;                                  // 线程数量上限阈值
    
    using Task = std::function<void()>;                         // 任务类型
    std::queue<Task> taskQue_;                                  // 这里需要保证传入的对象的声明周期要能执行完
    std::atomic_int taskSize_;                                  // 任务数量，因为要保证多线程安全所以需要使用原子变量
    int taskQueMaxThreshHold_;                                  // 任务队列数量上限阈值

    std::mutex taskQueMtx_;                                     // 保证任务队列的线程安全
    std::condition_variable notFull_;                           // 任务队列不满
    std::condition_variable notEmpty_;                          // 任务队列不空
    std::condition_variable exitCond_;                          // 线程池销毁时通知线程池中线程退出

    PoolMode poolMode_;                                         // 当前线程池的工作模式
    std::atomic_bool isPoolRunning_;                            // 当前线程池是否启动
};
