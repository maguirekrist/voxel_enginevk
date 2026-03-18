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
            while (true)
            {
                std::function<void()> job;
                _queue.wait_dequeue(job);

                if (!job)
                {
                    if (!_is_running.load(std::memory_order_acquire))
                    {
                        break;
                    }

                    continue;
                }

                job();
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
    if (!_is_running.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    for (int i = 0; i < _thread_count; ++i)
    {
        _queue.enqueue({});
    }

    for (auto& thread : _threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void ThreadPool::post(const std::function<void()>&& task)
{
    if (!_is_running.load(std::memory_order_acquire))
    {
        return;
    }

    _queue.enqueue(task);
}
