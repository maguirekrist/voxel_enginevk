
#include <chunk_manager.h>

void ChunkManager::updateChunksToLoad()
{
    std::vector<ChunkCoord> newChunks;
        for (int dx = -viewDistance; dx <= viewDistance; ++dx) {
            for (int dz = -viewDistance; dz <= viewDistance; ++dz) {
                ChunkCoord coord = {lastPlayerChunk.x + dx, lastPlayerChunk.z + dz};
                if (loadedChunks.find(coord) == loadedChunks.end()) {
                    newChunks.push_back(coord);
                }
            }
        }

        for (const auto& coord : newChunks) {
            chunksToLoad.push(coord);
        }

        // Identify chunks to unload
        std::vector<ChunkCoord> chunksToUnload;
        for (const auto& pair : loadedChunks) {
            int dx = std::abs(pair.first.x - lastPlayerChunk.x);
            int dz = std::abs(pair.first.z - lastPlayerChunk.z);
            if (dx > viewDistance || dz > viewDistance) {
                chunksToUnload.push_back(pair.first);
            }
        }

        // Unload chunks
        for (const auto& coord : chunksToUnload) {
            loadedChunks.erase(coord);
            chunkPool.push_back(std::move(loadedChunks[coord]));
        }
}

void ChunkManager::worker()
{
    while (running) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !chunksToLoad.empty() || !running; });

        if (!running) break;

        while (!chunksToLoad.empty() && loadedChunks.size() < maxChunks) {
            ChunkCoord coord = chunksToLoad.front();
            chunksToLoad.pop();

            lock.unlock();
            std::unique_ptr<Chunk> chunk;
            if (!chunkPool.empty()) {
                chunk = std::move(chunkPool.back());
                chunkPool.pop_back();
                // Reset chunk data here if necessary
            } else {
                chunk = std::make_unique<Chunk>(coord);
            }
            lock.lock();

            loadedChunks[coord] = std::move(chunk);
        }
    }
}

Block* ChunkManager::getBlockGlobal(int x, int y, int z)
{
    ChunkCoord chunkCoord = {x / CHUNK_SIZE, z / CHUNK_SIZE};
    int localX = x % CHUNK_SIZE;
    int localZ = z % CHUNK_SIZE;

    auto it = loadedChunks.find(chunkCoord);
    if (it != loadedChunks.end()) {
        return it->second->get_block({ localX, y, localZ });
    }
    return nullptr; // Or some default/error value
}