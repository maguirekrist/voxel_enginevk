#include <vk_types.h>

class Barrier {
public:
    Barrier(size_t count) : threadCount_(count), waiting_(0), flag_(false) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (++waiting_ == threadCount_) {
            // Last thread to arrive, flip the flag and reset waiting count
            waiting_ = 0;
            flag_ = !flag_;
            condition_.notify_all();
        } else {
            // Wait for the other threads
            bool flagSnapshot = flag_;
            condition_.wait(lock, [&]() { return flag_ != flagSnapshot; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    size_t threadCount_;
    size_t waiting_;
    bool flag_;
};

class Job : public std::enable_shared_from_this<Job> {
public:
    Job(std::function<void()> func, int priority = 0)
        : task(func), priority(priority), remainingDependencies(0) {}

    std::atomic<int> remainingDependencies;

    void addDependency(std::shared_ptr<Job> dependency) {
        std::lock_guard<std::mutex> lock(mutex);
        dependencies.push_back(dependency);
        remainingDependencies++;
    }

    void dependencyCompleted() {
        if (--remainingDependencies == 0) {
            if (onReady)
                onReady(shared_from_this());
        }
    }

    void execute() {
        try {
            task();
            promise.set_value();
            // Notify dependents
            if (onComplete)
                onComplete(shared_from_this());
        }
        catch (...) {
            promise.set_exception(std::current_exception());
        }
    }

    std::future<void> getFuture() {
        return promise.get_future();
    }

    void setOnReady(std::function<void(std::shared_ptr<Job>)> callback) {
        onReady = callback;
    }

    void setOnComplete(std::function<void(std::shared_ptr<Job>)> callback) {
        onComplete = callback;
    }

    int getPriority() const { return priority; }

private:
    std::function<void()> task;
    int priority;
    std::vector<std::shared_ptr<Job>> dependencies;
    std::mutex mutex;
    std::function<void(std::shared_ptr<Job>)> onReady;
    std::function<void(std::shared_ptr<Job>)> onComplete;
    std::promise<void> promise;
};

struct JobCompare {
    bool operator()(const std::shared_ptr<Job>& a, const std::shared_ptr<Job>& b) const {
        return a->getPriority() < b->getPriority();
    }
};

class JobQueue {
public:
    void push(std::shared_ptr<Job> job) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(job);
        condition.notify_one();
    }

    std::shared_ptr<Job> pop() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return !queue.empty() || stopFlag; });
        if (stopFlag && queue.empty())
            return nullptr;
        auto job = queue.top();
        queue.pop();
        return job;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex);
        stopFlag = true;
        condition.notify_all();
    }

private:
    std::priority_queue<std::shared_ptr<Job>, std::vector<std::shared_ptr<Job>>, JobCompare> queue;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopFlag = false;
};

class ThreadPool {
public:
    ThreadPool(size_t numThreads, JobQueue& jobQueue)
        : _jobQueue(jobQueue), stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this]() {
                while (!stop) {
                    auto job = _jobQueue.pop();
                    if (stop || !job)
                        break;
                    job->execute();
                }
            });
        }
    }

    ~ThreadPool() {
        stop = true;
        _jobQueue.stop();
        for (auto& worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    JobQueue& _jobQueue;
    std::atomic<bool> stop;
};

class JobScheduler {
public:
    JobScheduler(size_t numThreads)
        : threadPool(numThreads, jobQueue) {}

    std::shared_ptr<Job> createJob(std::function<void()> func, int priority = 0) {
        auto job = std::make_shared<Job>(func, priority);
        job->setOnReady([this](std::shared_ptr<Job> readyJob) {
            jobQueue.push(readyJob);
        });
        job->setOnComplete([this](std::shared_ptr<Job> completedJob) {
            // Notify dependents
            std::lock_guard<std::mutex> lock(depMutex);
            if (jobDependents.find(completedJob) != jobDependents.end()) {
                for (auto& dependent : jobDependents[completedJob]) {
                    dependent->dependencyCompleted();
                }
                jobDependents.erase(completedJob);
            }
        });

        // If no dependencies, enqueue immediately
        if (job->remainingDependencies == 0) {
            jobQueue.push(job);
        }

        return job;
    }

    void addDependency(std::shared_ptr<Job> job, std::shared_ptr<Job> dependency) {
        {
            std::lock_guard<std::mutex> lock(depMutex);
            jobDependents[dependency].push_back(job);
        }
        job->addDependency(dependency);
    }

private:
    JobQueue jobQueue;
    ThreadPool threadPool;
    std::unordered_map<std::shared_ptr<Job>, std::vector<std::shared_ptr<Job>>> jobDependents;
    std::mutex depMutex;
};