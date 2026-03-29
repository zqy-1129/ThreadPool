#include <iostream>

#include "ThreadPool.h"

int main() {
    ThreadPool pool;
    pool.start();

    int x;
    std::cin >> x;
    return 0;
}