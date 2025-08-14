//
// Created by Maguire Krist on 8/13/25.
//

#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <functional>
#include <thread>
#include <vector>

#include "utils/blockingconcurrentqueue.h"


class ThreadPool {

    bool _is_running = true;
    const int _thread_count;
    std::vector<std::thread> _threads;
    moodycamel::BlockingConcurrentQueue<std::function<void()>> _queue;
public:
    explicit ThreadPool(int thread_count);
    ~ThreadPool();
    void stop();
    void post(const std::function<void()>&& task);
};



#endif //THREAD_POOL_H
