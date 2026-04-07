#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>
#include <mutex>
#include <unordered_map>

// Any类型：可以接受任意类型的数据
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;
    // 这里使用基类的指针指向子类的对象，而这个子类是可以接受任意类型
    // 这就实现了对任意类型的接受
    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}  // 这里就是构造Any类型，发现其可以接受任意类型进行构造

    // 这个方法能将Any对象里面存储的data数据提取出来
    template<typename T>
    T cast_() 
    {   
        // 基类指针转换成派生类对象 RTTI
        Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) 
        {
            throw "type is unmatch!";
        }
        return pd->data_;
    }
private: 
    // 基类类型
    class Base 
    {
    public:
        virtual ~Base() = default;
    };

    template<typename T> 
    class Derive : public Base
    {   
    public:
        Derive(T data) : data_(data) {}
        T data_;
    };

    // 定义基类指针
    std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(limit) {}
    ~Semaphore() = default;

    // 获取一个信号量资源
    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        // 等待信号量有资源，没有资源的化会阻塞当前线程
        cond_.wait(lock, [&]()->bool {return resLimit_ > 0;});
        resLimit_--;
    }

    // 增加一个信号量资源
    void post() 
    {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

// 实现接受提交到线程池的task任务执行完成后的返回值类型Result
// 这里由于提交线程和执行线程的函数不是一个线程所以需要信号量来控制

class Task;

class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    Result(Result&&) = default;
    Result& operator=(Result&&) = default;

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    // 获取返回值的接口
    Any get();

    // 设置返回值
    void setVal(Any any);
private:
    Any any_;                       // 存储任务的返回值 
    Semaphore sem_;                 // 线程通信信号量
    std::shared_ptr<Task> task_;    // 指向对应的获取返回值的任务对象
    std::atomic_bool isValid_;      // 返回值是否有效
};

// 任务抽象基类
// 用户可以子等以任意类型，从Task继承，重写run方法，实现自定义任务处理
class Task
{
public:
    Task();
    ~Task() = default;

    void exec();
    void setResult(Result* res);
    virtual Any run() = 0;
private:
    Result* result_;    // Result声明周期大于Task
};

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
    Thread(ThreadFunc func);

    // 线程析构
    ~Thread();

    // 启动线程
    void start();

    // 获取线程Id
    int getId() const;
private:
    ThreadFunc func_;
    static int gernerateId_;
    int threadId_;
};

/*
    example:
    ThreadPool pool;
    pool.start();

    class MyTask : public Task
    {
    public:    
        MyTask(int args) : args_(args) {}
        void run() override
        {           
            // 任务处理逻辑
        }
    private:    int args_;
    };  

    pool.submitTask(std::make_shared<MyTask>(args));
*/

// 线程池
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    // 开启线程池
    void start(int initThreadSize = 4);
    
    // 设置线程池模式
    void setMode(PoolMode mode);

    // 设置任务队列上限的阈值
    void setTaskQueMaxThreshHold(int threshHold);

    // 设置线程池cached模式先线程的阈值
    void setThreadSizeThreshHold(int threshHold);

    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
private:
    // 线程执行函数
    void threadFunc(int threadId);

    // 检查运行状态
    bool checkRunningState() const;

    // std::vector<std::unique_ptr<Thread>> threads_;  // 线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表
    size_t initThreadSize_;                                     // 初始线程数量
    std::atomic_int currThreadSize_;                            // 当前线程数
    std::atomic_int idleThreadSize_;                            // 记录空闲线程的数量
    int threadSizeThreshHold_;                                  // 线程数量上限阈值

    std::queue<std::shared_ptr<Task>> taskQue_;                 // 这里需要保证传入的对象的声明周期要能执行完
    std::atomic_int taskSize_;                                  // 任务数量，因为要保证多线程安全所以需要使用原子变量
    int taskQueMaxThreshHold_;                                  // 任务队列数量上限阈值

    std::mutex taskQueMtx_;                                     // 保证任务队列的线程安全
    std::condition_variable notFull_;                           // 任务队列不满
    std::condition_variable notEmpty_;                          // 任务队列不空
    std::condition_variable exitCond_;                          // 线程池销毁时通知线程池中线程退出

    PoolMode poolMode_;                                         // 当前线程池的工作模式
    std::atomic_bool isPoolRunning_;                            // 当前线程池是否启动
};
