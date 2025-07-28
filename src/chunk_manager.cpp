
#include "chunk_manager.h"
#include "game/chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include "vk_mesh.h"
#include <memory>
#include "vk_engine.h"

void ChunkWorkQueue::enqueue(const ChunkWork& work)
{
    switch (work.phase)
    {
    case ChunkWork::Phase::Generate:
        _highPriority.enqueue(work);
        break;
    case ChunkWork::Phase::Mesh:
        _medPriority.enqueue(work);
        break;
    case ChunkWork::Phase::WaitingForNeighbors:
        _lowPriority.enqueue(work);
        break;
    }
}

bool ChunkWorkQueue::try_dequeue(ChunkWork& work)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.try_dequeue(work);
}

void ChunkWorkQueue::wait_dequeue(ChunkWork& work)
{
    if (_highPriority.try_dequeue(work)) return;
    if (_medPriority.try_dequeue(work)) return;
    _lowPriority.wait_dequeue(work);
}

bool ChunkWorkQueue::wait_dequeue_timed(ChunkWork& work, const int timeout_ms)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.wait_dequeue_timed(work, std::chrono::milliseconds(timeout_ms));
}

size_t ChunkWorkQueue::size_approx() const
{
    return _highPriority.size_approx() + _lowPriority.size_approx() + _medPriority.size_approx();
}

ChunkManager::ChunkManager()
        :
        _viewDistance(DEFAULT_VIEW_DISTANCE),
          _maxChunks((2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1)),
          _maxThreads(4),
          _running(true)
{

    _chunks.reserve(_maxChunks);
    _renderedChunks.reserve(_maxChunks);
    _worldChunks.reserve(_maxChunks);

    auto max_thread = std::thread::hardware_concurrency();
    _maxThreads = max_thread != 0 ? max_thread : _maxThreads;

    for(size_t i = 0; i < _maxThreads; i++)
    {
        _workers.emplace_back(&ChunkManager::work_chunk, this, i);
    }
}

static std::tuple<int, int, int, int> get_chunk_range(auto&& coords)
{
    int minX = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int minZ = std::numeric_limits<int>::max();
    int maxZ = std::numeric_limits<int>::min();

    for (const ChunkCoord& coord : coords) {
        minX = std::min(minX, coord.x);
        maxX = std::max(maxX, coord.x);
        minZ = std::min(minZ, coord.z);
        maxZ = std::max(maxZ, coord.z);
    }

    return std::tuple{minX, maxX, minZ, maxZ};
}

void ChunkManager::cleanup()
{
    std::println("ChunkManager::cleanup");
    _renderedChunks.clear();
    _transparentObjects.clear();
    for(const auto& [key, chunk] : _chunks)
    {
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
    }

    _chunks.clear();
}

ChunkManager::~ChunkManager()
{
    _running = false;

    for (std::thread &worker : _workers) {
        worker.join();
    }

    std::println("ChunkManager::~ChunkManager");
}

void ChunkManager::update_player_position(const int x, const int z)
{
    const ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk && !_initialLoad) return;

    const auto changeX = playerChunk.x - _lastPlayerChunk.x;
    const auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    std::println("Player changed chunks, delta: x {},  z {}", changeX, changeZ);
    std::println("Player position: x {},  z {}", playerChunk.x, playerChunk.z);
    _lastPlayerChunk = playerChunk;
    std::vector<ChunkCoord> oldChunks = std::vector<ChunkCoord>(_maxChunks);

    oldChunks.swap(_worldChunks);
    update_world_state();

    auto [wlx, whx, wlz, whz] = get_chunk_range(_worldChunks);
    _mapRange.low_x.store(wlx);
    _mapRange.low_z.store(wlz);
    _mapRange.high_x.store(whx);
    _mapRange.high_z.store(whz);

    if (_initialLoad)
    {
        oldChunks = _worldChunks;
        _initialLoad = false;
    }

    _renderedChunks.clear();
    _transparentObjects.clear();

    //calculate old chunks and remove.
    for (const auto& chunkCoord : oldChunks)
    {
        if (chunkCoord.x < wlx ||
            chunkCoord.x > whx ||
            chunkCoord.z < wlz ||
            chunkCoord.z > whz)
        {
            std::shared_lock lock(_mapMutex);
            if (_chunks.contains(chunkCoord))
            {
                lock.unlock();
                std::unique_lock unique(_mapMutex);
                auto chunk = _chunks.at(chunkCoord);
                VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
                VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
                _chunks.erase(chunkCoord);
            } else
            {
                throw std::runtime_error("Chunk does not exist");
            }
        }
    }

    std::vector<ChunkWork> WorkQueue;
    WorkQueue.reserve(_maxChunks);

    for(const auto& chunkCoord : _worldChunks)
    {
        if (!_chunks.contains(chunkCoord))
        {
            auto unchunked = std::make_shared<Chunk>(chunkCoord);
            std::unique_lock lock(_mapMutex);
            _chunks.insert({ chunkCoord, unchunked });
            //defer queue -> _chunkWorkQueue.enqueue(ChunkWork { .chunk = unchunked, .phase = ChunkWork::Phase::Generate });
            WorkQueue.emplace_back(unchunked, ChunkWork::Phase::Generate);
        }

        auto chunkToRender = _chunks.at(chunkCoord);
        if (chunk_at_border(chunkCoord)) {  continue; } //do not render chunks that are at the border. Just Generate.

        if (chunkToRender->_state.load() == ChunkState::Border)
        {
            chunkToRender->_state.store(ChunkState::Generated);
            WorkQueue.emplace_back(chunkToRender, ChunkWork::Phase::WaitingForNeighbors);
            // _chunkWorkQueue.enqueue(ChunkWork { .chunk = chunkToRender, .phase = ChunkWork::Phase::WaitingForNeighbors });
        }

        auto object = std::make_unique<RenderObject>(RenderObject{
               chunkToRender->_mesh,
               VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
               glm::ivec2(chunkToRender->_position.x, chunkToRender->_position.y),
               RenderLayer::Opaque
           });
        _renderedChunks.push_back(std::move(object));

        auto waterObject = std::make_unique<RenderObject>(RenderObject{
            chunkToRender->_waterMesh,
            VulkanEngine::instance()._materialManager.get_material("watermesh"),
            glm::ivec2(chunkToRender->_position.x, chunkToRender->_position.y),
            RenderLayer::Transparent
        });
        _transparentObjects.push_back(std::move(waterObject));
    }

    for (const auto& chunkWork : WorkQueue)
    {
        _chunkWorkQueue.enqueue(std::move(chunkWork));
    }
    WorkQueue.clear();

    std::println("Active chunks: {}", _chunks.size());
    std::println("Renderable chunks: {}", _renderedChunks.size());
    std::println("(world chunks): {}", _worldChunks.size());
    std::println("Work Queue: {}", _chunkWorkQueue.size_approx());
}

void ChunkManager::update_world_state()
{
    _worldChunks.clear();
    _worldChunks.reserve(_maxChunks);//is reserve needed after clear?
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            _worldChunks.push_back(coord);
        }
    }
}

bool ChunkManager::chunk_at_border(const ChunkCoord coord) const
{
    return (coord.x == _mapRange.low_x.load() ||
            coord.x == _mapRange.high_x.load() ||
            coord.z == _mapRange.low_z.load() ||
            coord.z == _mapRange.high_z.load());
}

NeighborStatus ChunkManager::chunk_has_neighbors(const ChunkCoord coord)
{
    int count = 0;
    std::shared_lock lock(_mapMutex);

    if (!_chunks.contains(coord))
    {
        std::println("Chunk does not exist: x {}, z {}", coord.x, coord.z);
        return NeighborStatus::Missing;
    }

    auto chunk = _chunks.at(coord);
    if (chunk->_state.load() == ChunkState::Border)
    {
        std::println("{} at border...", coord);
        return NeighborStatus::Border;
    }

    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if (_chunks.contains(offset_coord))
        {
            const auto neighbor_chunk = _chunks.at(offset_coord);
            switch (neighbor_chunk->_state.load())
            {
                case ChunkState::Uninitialized:
                    return NeighborStatus::Incomplete;
                case ChunkState::Border:
                case ChunkState::Generated:
                case ChunkState::Rendered:
                    count++;
                    break;
            }
        } else
        {
            std::println("{} not in map", offset_coord);
        }
    }

    if(count == 8)
    {
        return NeighborStatus::Ready;
    }

    //std::println("{} not ready...", coord);
    return NeighborStatus::Incomplete;
}

std::optional<std::array<std::shared_ptr<Chunk>, 8>> ChunkManager::get_chunk_neighbors(const ChunkCoord coord)
{
    ZoneScopedN("Get Chunk Neighbors");
    std::array<std::shared_ptr<Chunk>, 8> chunks;
    int count = 0;
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if(const auto chunk = get_chunk(offset_coord))
        {
            if (!chunk.has_value()) return std::nullopt;
            if (chunk.value()->_state.load() == ChunkState::Uninitialized) return std::nullopt;
            chunks[direction] = chunk.value();
            count++;
        }
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}

int ChunkManager::get_chunk_index(const ChunkCoord coord) const
{
    //get a unique index for the chunk coord
    const int x = coord.x - _lastPlayerChunk.x;
    const int y = coord.z - _lastPlayerChunk.z;

    // Normalize the relative coordinates to the range [0, 64] by adding viewDistance
    const int normalizedX = std::clamp(x + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);
    const int normalizedZ = std::clamp(y + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);

    // Calculate the 1D index in the vector
    const int index = normalizedZ * (2 * DEFAULT_VIEW_DISTANCE + 1) + normalizedX;

    return index;
}

std::optional<std::shared_ptr<Chunk>> ChunkManager::get_chunk(const ChunkCoord coord)
{
    std::shared_lock lock(_mapMutex);
    if (_chunks.contains(coord))
    {

        return _chunks.at(coord);
    }
    return std::nullopt;
}

// void ChunkManager::save_chunk(const Chunk &chunk, const std::string &filename)
// {
//     std::ofstream outFile(filename, std::ios::binary);
//
//     outFile.write(reinterpret_cast<const char*>(&chunk._position), sizeof(chunk._position));
//
//     for (const auto& block : chunk._blocks)
//     {
//         outFile.write(reinterpret_cast<const char*>(&block), sizeof(block));
//     }
//
//     outFile.close();
// }

// std::unique_ptr<Chunk> ChunkManager::load_chunk(const std::string &filename)
// {
//     auto chunk = std::make_unique<Chunk>();
//
//     std::ifstream inFile(filename, std::ios::binary);
//
//     inFile.read(reinterpret_cast<char*>(&chunk->_position), sizeof(chunk->_position));
//
//     for (auto& block : chunk->_blocks)
//     {
//         inFile.read(reinterpret_cast<char *>(&block), sizeof(block));
//     }
//
//     inFile.close();
//     return chunk;
// }



void ChunkManager::work_chunk(int threadId)
{
    tracy::SetThreadName(std::format("Chunk Update Thread: {}", threadId).c_str());
    while(_running)
    {
        if (ChunkWork work_item; _chunkWorkQueue.try_dequeue(work_item))
        {
            switch (work_item.phase)
            {
                case ChunkWork::Phase::Generate:
                    work_item.chunk->generate();
                    if (chunk_at_border(work_item.chunk->_chunkCoord))
                    {
                        work_item.chunk->_state.store(ChunkState::Border);
                        break;
                    }
                    work_item.phase = ChunkWork::Phase::WaitingForNeighbors;
                    _chunkWorkQueue.enqueue(work_item);
                    break;
                case ChunkWork::Phase::WaitingForNeighbors:
                    switch(chunk_has_neighbors(work_item.chunk->_chunkCoord))
                    {
                        case NeighborStatus::Ready:
                            work_item.phase = ChunkWork::Phase::Mesh;
                            _chunkWorkQueue.enqueue(work_item);
                            break;
                        case NeighborStatus::Incomplete:
                            //std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            _chunkWorkQueue.enqueue(work_item);
                            break;
                        case NeighborStatus::Missing:
                        case NeighborStatus::Border:
                            break;
                    }
                    break;
                case ChunkWork::Phase::Mesh:
                    auto neighbors = get_chunk_neighbors(work_item.chunk->_chunkCoord);
                    ChunkMesher mesher { work_item.chunk, neighbors };
                    mesher.generate_mesh();
                    work_item.chunk->_state.store(ChunkState::Rendered);

                    if(!work_item.chunk->_waterMesh->_vertices.empty())
                    {
                        VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item.chunk->_waterMesh);
                    }
                    VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item.chunk->_mesh);
                    break;
            }
        } else if (!_running)
        {
            break;
        }
    }
}
