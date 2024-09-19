#include <vk_types.h>

class Barrier {
public:
    Barrier(int count) : threshold(count), count(count), generation(0) {}
    
    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        int gen = generation;
        
        if (--count == 0) {
            generation++;
            count = threshold;
            cv.notify_all();
        } else {
            cv.wait(lock, [this, gen]() { return gen != generation; });
        }
    }

private:
    std::mutex mtx;
    std::condition_variable cv;
    int threshold;
    int count;
    int generation;
};
