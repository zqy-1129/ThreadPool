#include "ThreadPool.h"

#include <iostream>

const int TASK_MAX_THRESHHOLD= 1024;

// 线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(4)
    , taskSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
{}

// 线程池析构
ThreadPool::~ThreadPool()
{}

// 开启线程池
void ThreadPool::start(int initThreadSize) 
{   
    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    // 创建线程对象
    for (int i = 0; i < initThreadSize_; i++) 
    {   
        // 创建线程对象时候，将线程函数给线程对象
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));   // 这里是14的新特性
        threads_.emplace_back(std::move(ptr));
    }

    // 启动所有线程
    for (int i = 0; i < initThreadSize_; i++) 
    {
        threads_[i]->start();       // 需要一个线程函数
    }
}

// 设置线程池模式
void ThreadPool::setMode(PoolMode mode)
{
    poolMode_ = mode;
}

// 设置任务队列上限的阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshHold)
{
    taskQueMaxThreshHold_ = threshHold;
}

// 给线程池提交任务 用户调用该接口传入任务对象
void ThreadPool::submitTask(std::shared_ptr<Task> sp)
{

}

 // 线程执行函数 线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc()
{
    std::cout << "begin threadFunc tid: " << std::this_thread::get_id() << std::endl;
    std::cout << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
}

// 线程构造
Thread::Thread(ThreadFunc func) : func_(func)
{}

// 线程析构
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
    // 创建线程执行线程函数
    std::thread t(func_);
    // 设置分离线程
    t.detach();     
}