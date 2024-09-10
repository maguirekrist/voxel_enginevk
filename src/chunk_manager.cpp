
#include <chunk_manager.h>

ChunkManager::ChunkManager(size_t initialPoolSize) : poolSize_(initialPoolSize) {
    for (size_t i = 0; i < poolSize_; i++)
    {
        //pool_.push_back(Chunk{});
    }
}