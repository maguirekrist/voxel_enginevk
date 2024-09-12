#pragma once

#include <vk_types.h>
#include <chunk.h>



class ChunkManager {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> loadedChunks;

    ChunkManager(int viewDistance) 
        : viewDistance(viewDistance), 
          maxChunks((2 * viewDistance + 1) * (2 * viewDistance + 1)),
          running(true) {
        chunkPool.reserve(maxChunks);
        workerThread = std::thread(&ChunkManager::worker, this);
    }

    ~ChunkManager() {
        running = false;
        cv.notify_one();
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void updatePlayerPosition(int x, int z) {
        ChunkCoord playerChunk = {x / CHUNK_SIZE, z / CHUNK_SIZE};  // Assuming 16x16 chunks
        if (playerChunk == lastPlayerChunk) return;

        std::unique_lock<std::mutex> lock(mutex);
        lastPlayerChunk = playerChunk;
        updateChunksToLoad();
        cv.notify_one();
    }

    void printLoadedChunks() {
        std::unique_lock<std::mutex> lock(mutex);
        std::cout << "Loaded chunks: ";
        for (const auto& chunk : loadedChunks) {
            std::cout << "(" << chunk.first.x << "," << chunk.first.z << ") ";
        }
        std::cout << std::endl;
    }

private:
    void updateChunksToLoad();

    void worker();

    Block* getBlockGlobal(int x, int y, int z);



    int viewDistance;
    size_t maxChunks;
    ChunkCoord lastPlayerChunk = {0, 0};
    std::vector<std::unique_ptr<Chunk>> chunkPool;
    std::queue<ChunkCoord> chunksToLoad;
    std::thread workerThread;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> running;
};