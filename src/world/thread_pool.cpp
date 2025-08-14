//
// Created by Maguire Krist on 8/13/25.
//

#include "thread_pool.h"

ThreadPool::ThreadPool(const int thread_count) : _thread_count(thread_count)
{
    for (int i = 0; i < thread_count; i++)
    {
        _threads.emplace_back([this]()
        {
            while (this->_is_running)
            {
                std::function<void()> job;
                if (_queue.try_dequeue(job))
                {
                    job();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::stop()
{
    _is_running = false;
    for (auto& thread : _threads)
    {
        thread.join();
    }
}

void ThreadPool::post(const std::function<void()>&& task)
{
    _queue.enqueue(task);
}
