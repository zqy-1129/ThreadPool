#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

#include "ThreadPool.h"

int sum1(int a, int b) 
{   
    this_thread::sleep_for(chrono::seconds(3)); // 模拟耗时任务
    return a + b;
}

int sum2(int a, int b, int c) 
{
    this_thread::sleep_for(chrono::seconds(3)); // 模拟耗时任务
    return a + b + c;
}

int main() 
{  
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.setMode(PoolMode::MODE_FIXED);
    pool.start(4);
    
    vector<future<int>> results;
    for (int i = 0; i < 4; i++) {
        results.emplace_back(pool.submitTask(sum1, i, i + 10));
        results.emplace_back(pool.submitTask(sum2, i, i + 10, i + 20));
    }

    this_thread::sleep_for(chrono::seconds(5)); // 等待任务执行完成
    for (auto& result : results) {
        cout << "Result: " << result.get() << endl;
    }
    return 0;
}