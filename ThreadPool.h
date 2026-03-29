#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>

// 任务抽象基类
// 用户可以子等以任意类型，从Task继承，重写run方法，实现自定义任务处理
class Task
{
public:
    virtual void run() = 0;
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
    using ThreadFunc = std::function<void()>;
    // 线程构造
    Thread(ThreadFunc func);

    // 线程析构
    ~Thread();

    // 启动线程
    void start();
private:
    ThreadFunc func_;
};

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

    // 给线程池提交任务
    void submitTask(std::shared_ptr<Task> sp);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
private:
    // 线程执行函数
    void threadFunc();

    std::vector<std::unique_ptr<Thread>> threads_;  // 线程列表
    size_t initThreadSize_;                         // 初始线程数量

    std::queue<std::shared_ptr<Task>> taskQue_;     // 这里需要保证传入的对象的声明周期要能执行完
    std::atomic_int taskSize_;                      // 任务数量，因为要保证多线程安全所以需要使用原子变量
    int taskQueMaxThreshHold_;                      // 任务队列数量上限阈值

    std::condition_variable notFull_;               // 任务队列不满
    std::condition_variable notEmpty_;              // 任务队列不空

    PoolMode poolMode_;                             // 当前线程池的工作模式
};
