#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

#include "ThreadPool.h"

/**
 * 线程池任务的返回值可能根据需求不同
 */

class MyTask : public Task 
{
public:
    MyTask(int begin, int end) : begin_(begin), end_(end) {}
    Any run() 
    {   
        std::this_thread::sleep_for(std::chrono::seconds(5));
        cout << "tid: " << this_thread::get_id() << " begin!" << endl;
        int sum = 0;
        for (int i = begin_; i <= end_; i++) sum += i;
        cout << "tid: " << this_thread::get_id() << " end!" << endl;
        return sum;
    }
private:
    int begin_;
    int end_;
};

int main() {
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(2);

        // 一次性提交10个任务
        pool.submitTask(std::make_shared<MyTask>(1, 100));
        pool.submitTask(std::make_shared<MyTask>(1, 100));
        pool.submitTask(std::make_shared<MyTask>(1, 100));
        pool.submitTask(std::make_shared<MyTask>(1, 100));

        std::cout << "等待结果..." << std::endl;
        getchar();  // 等待用户输入，防止主线程过早退出
    }
    
    return 0;
}