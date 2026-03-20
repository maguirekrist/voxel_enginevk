
#include "chunk_manager.h"
#include "../game/block.h"
#include "../game/chunk.h"
#include "../game/world.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include <algorithm>
#include <memory>

namespace
{
    constexpr int MaxGenerateJobsPerTick = 8;
    constexpr int MaxLightJobsPerTick = 8;
    constexpr int MaxMeshJobsPerTick = 8;

    [[nodiscard]] int chunk_distance_sq(const ChunkCoord a, const ChunkCoord b) noexcept
    {
        const int dx = a.x - b.x;
        const int dz = a.z - b.z;
        return (dx * dx) + (dz * dz);
    }
}

ChunkManager::ChunkManager() : m_chunkCache(nullptr)
{
}

ChunkManager::~ChunkManager() = default;

void ChunkManager::update_player_position(const glm::vec3& position)
{
    const ChunkCoord playerChunk = World::get_chunk_coordinates(position);

    if (playerChunk != _lastPlayerChunk || _initialLoad)
    {
        const auto changeX = playerChunk.x - _lastPlayerChunk.x;
        const auto changeZ = playerChunk.z - _lastPlayerChunk.z;
        _lastPlayerChunk = playerChunk;
        const MapRange mapRange(playerChunk, _viewDistance);

        if (_initialLoad)
        {
            _initialLoad = false;
            initialize_map(mapRange);
        }
        else
        {
            auto new_chunks = m_chunkCache->slide({ changeX, changeZ });

            for (Chunk* const chunk : new_chunks)
            {
                reset_chunk_runtime(chunk);
                _renderResetEvents.enqueue(ChunkRenderResetEvent{
                    .chunk = chunk,
                    .generation = chunk->_gen.load(std::memory_order::acquire)
                });
            }

        }
    }

    drain_generate_results();
    apply_pending_world_edits();
    drain_light_results();
    drain_mesh_results();
    run_scheduler();
}

Chunk* ChunkManager::get_chunk(const ChunkCoord coord) const
{
    if (m_chunkCache == nullptr)
    {
        return nullptr;
    }
    return m_chunkCache->get_chunk(coord);
}

std::optional<ChunkManager::ChunkDebugState> ChunkManager::debug_state(const ChunkCoord coord) const
{
    const Chunk* const chunk = get_chunk(coord);
    const ChunkRuntime* const runtime = runtime_for(chunk);
    if (runtime == nullptr)
    {
        return std::nullopt;
    }

    const ChunkRecord& record = runtime->record;
    return ChunkDebugState{
        .resident = record.residency == ChunkResidencyState::Resident,
        .generationId = record.chunkGenerationId,
        .dataVersion = record.dataVersion,
        .lightVersion = record.lightVersion,
        .litAgainstSignature = record.litAgainstSignature,
        .meshedAgainstSignature = record.meshedAgainstSignature,
        .uploadedSignature = record.uploadedSignature,
        .dataState = record.dataState,
        .lightState = record.lightState,
        .meshState = record.meshState,
        .generationJobInFlight = record.generationJobInFlight,
        .lightJobInFlight = record.lightJobInFlight,
        .meshJobInFlight = record.meshJobInFlight,
        .uploadPending = record.uploadPending
    };
}

void ChunkManager::initialize_map(const MapRange mapRange)
{
    m_chunkCache = std::make_unique<ChunkCache>(_viewDistance);
    _runtimeByChunk.clear();

    for (auto& chunk : m_chunkCache->m_chunks)
    {
        reset_chunk_runtime(chunk.get());
        _renderResetEvents.enqueue(ChunkRenderResetEvent{
            .chunk = chunk.get(),
            .generation = chunk->_gen.load(std::memory_order::acquire)
        });
    }
}

void ChunkManager::enqueue_block_edit(const BlockEdit& edit)
{
    _worldEditQueue.enqueue(edit);
}

void ChunkManager::set_ambient_occlusion_enabled(const bool enabled)
{
    if (_ambientOcclusionEnabled == enabled)
    {
        return;
    }

    _ambientOcclusionEnabled = enabled;
    for (auto& [chunk, runtime] : _runtimeByChunk)
    {
        ChunkRecord& record = runtime.record;
        if (chunk == nullptr || record.data == nullptr)
        {
            continue;
        }

        record.meshState = MeshState::Stale;
        record.uploadPending = false;
        record.meshedAgainstSignature = 0;
        record.uploadedSignature = 0;
        chunk->_state.store(ChunkState::Generated, std::memory_order::release);
    }
}

bool ChunkManager::ambient_occlusion_enabled() const noexcept
{
    return _ambientOcclusionEnabled;
}

void ChunkManager::set_view_distance(const int viewDistance)
{
    const int clampedViewDistance = std::max(1, viewDistance);
    if (_viewDistance == clampedViewDistance)
    {
        return;
    }

    _viewDistance = clampedViewDistance;
    initialize_map(MapRange(_lastPlayerChunk, _viewDistance));
}

int ChunkManager::view_distance() const noexcept
{
    return _viewDistance;
}

void ChunkManager::notify_chunk_uploaded(Chunk* chunk, const uint32_t generationId, const uint64_t neighborhoodSignature)
{
    if (chunk == nullptr)
    {
        return;
    }

    if (chunk->_gen.load(std::memory_order::acquire) != generationId)
    {
        return;
    }

    if (ChunkRuntime* const runtime = runtime_for(chunk))
    {
        ChunkRecord& record = runtime->record;
        if (record.meshedAgainstSignature != neighborhoodSignature)
        {
            return;
        }

        record.uploadedSignature = neighborhoodSignature;
        record.meshState = MeshState::Uploaded;
        record.uploadPending = false;
        chunk->_state.store(ChunkState::Rendered, std::memory_order::release);
    }
}

void ChunkManager::drain_generate_results()
{
    ChunkGenerateResult result;
    while (_generateResults.try_dequeue(result))
    {
        if (result.chunk == nullptr || result.chunk->_gen.load(std::memory_order::acquire) != result.generationId)
        {
            continue;
        }

        ChunkRuntime* const runtime = runtime_for(result.chunk);
        if (runtime == nullptr)
        {
            continue;
        }

        ChunkRecord& record = runtime->record;
        record.generationJobInFlight = false;
        record.data = std::move(result.data);
        result.chunk->_data = record.data;
        record.dataState = DataState::Ready;
        record.dataVersion += 1;
        record.lightVersion = 0;
        record.litAgainstSignature = 0;
        record.lightState = LightState::Missing;
        record.meshState = MeshState::Missing;
        record.meshedAgainstSignature = 0;
        record.uploadedSignature = 0;
        record.uploadPending = false;
        result.chunk->_state.store(ChunkState::Generated, std::memory_order::release);
    }
}

void ChunkManager::drain_light_results()
{
    ChunkLightBuildResult result;
    while (_lightResults.try_dequeue(result))
    {
        if (result.chunk == nullptr || result.chunk->_gen.load(std::memory_order::acquire) != result.generationId)
        {
            continue;
        }

        ChunkRuntime* const runtime = runtime_for(result.chunk);
        if (runtime == nullptr)
        {
            continue;
        }

        ChunkRecord& record = runtime->record;
        record.lightJobInFlight = false;

        ChunkNeighborhood neighborhood{};
        uint64_t currentSignature = 0;
        if (!required_neighbors_have_data(record.coord, currentSignature, neighborhood))
        {
            record.lightState = LightState::Stale;
            continue;
        }

        if (record.dataVersion != result.dataVersion || currentSignature != result.neighborhoodSignature)
        {
            record.lightState = LightState::Stale;
            continue;
        }

        record.data = std::move(result.litData);
        result.chunk->_data = record.data;
        record.dataState = DataState::Ready;
        record.lightVersion += 1;
        record.litAgainstSignature = result.neighborhoodSignature;
        record.lightState = LightState::Ready;
        record.meshState = MeshState::Missing;
        record.meshedAgainstSignature = 0;
        record.uploadPending = false;
        result.chunk->_state.store(ChunkState::Generated, std::memory_order::release);
    }
}

void ChunkManager::drain_mesh_results()
{
    ChunkMeshBuildResult result;
    while (_meshResults.try_dequeue(result))
    {
        if (result.chunk == nullptr || result.chunk->_gen.load(std::memory_order::acquire) != result.generationId)
        {
            continue;
        }

        ChunkRuntime* const runtime = runtime_for(result.chunk);
        if (runtime == nullptr)
        {
            continue;
        }

        ChunkRecord& record = runtime->record;
        record.meshJobInFlight = false;

        ChunkNeighborhood neighborhood{};
        uint64_t currentSignature = 0;
        if (!required_neighbors_have_lighting(record.coord, currentSignature, neighborhood))
        {
            record.meshState = MeshState::Stale;
            continue;
        }

        if (record.dataVersion != result.dataVersion || currentSignature != result.neighborhoodSignature)
        {
            record.meshState = MeshState::Stale;
            continue;
        }

        record.mesh = std::move(result.meshData);
        result.chunk->_meshData = record.mesh;
        record.meshedAgainstSignature = result.neighborhoodSignature;
        record.meshState = MeshState::MeshReady;
    }
}

void ChunkManager::apply_pending_world_edits()
{
    while (const std::optional<BlockEdit> edit = _worldEditQueue.try_dequeue())
    {
        Chunk* const ownerChunk = get_chunk(World::get_chunk_coordinates(edit->worldPos));
        if (ownerChunk == nullptr)
        {
            continue;
        }

        ChunkRuntime* const ownerRuntime = runtime_for(ownerChunk);
        if (ownerRuntime == nullptr)
        {
            continue;
        }

        ChunkRecord& ownerRecord = ownerRuntime->record;
        if (ownerRecord.data == nullptr || (ownerRecord.dataState != DataState::Ready && ownerRecord.dataState != DataState::Dirty))
        {
            continue;
        }

        const glm::ivec3 localPos = ownerRecord.data->to_local_position(edit->worldPos);
        if (Chunk::is_outside_chunk(localPos))
        {
            continue;
        }

        Block& existingBlock = ownerRecord.data->blocks[localPos.x][localPos.y][localPos.z];
        const bool blockChanged = existingBlock._solid != edit->newBlock._solid ||
            existingBlock._type != edit->newBlock._type;
        if (!blockChanged)
        {
            continue;
        }

        const bool opacityChanged = existingBlock._solid != edit->newBlock._solid;
        Block updatedBlock = edit->newBlock;
        updatedBlock._sunlight = 0;
        updatedBlock._localLight = {};
        existingBlock = updatedBlock;

        for (const DirtyChunkMark& mark : _dirtyTracker.affected_chunks(ownerRecord.coord, localPos))
        {
            if (Chunk* const dirtyChunk = get_chunk(mark.coord))
            {
                mark_chunk_dirty(dirtyChunk, dirtyChunk == ownerChunk, opacityChanged);
            }
        }
    }
}

void ChunkManager::run_scheduler()
{
    if (m_chunkCache == nullptr)
    {
        return;
    }

    std::vector<Chunk*> prioritizedChunks{};
    prioritizedChunks.reserve(m_chunkCache->m_chunks.size());
    for (const auto& chunkPtr : m_chunkCache->m_chunks)
    {
        prioritizedChunks.push_back(chunkPtr.get());
    }

    std::ranges::sort(prioritizedChunks, [this](const Chunk* lhs, const Chunk* rhs)
    {
        const ChunkCoord lhsCoord = lhs != nullptr ? lhs->_data->coord : ChunkCoord{};
        const ChunkCoord rhsCoord = rhs != nullptr ? rhs->_data->coord : ChunkCoord{};
        return chunk_distance_sq(lhsCoord, _lastPlayerChunk) < chunk_distance_sq(rhsCoord, _lastPlayerChunk);
    });

    int generateJobsQueued = 0;
    int lightJobsQueued = 0;
    int meshJobsQueued = 0;

    for (Chunk* const chunk : prioritizedChunks)
    {
        ChunkRuntime* const runtime = runtime_for(chunk);
        if (runtime == nullptr)
        {
            continue;
        }

        ChunkRecord& record = runtime->record;
        if (generateJobsQueued < MaxGenerateJobsPerTick && _scheduler.should_generate(record))
        {
            queue_generate(chunk);
            ++generateJobsQueued;
            continue;
        }

        ChunkNeighborhood neighborhood{};
        uint64_t lightSignature = 0;
        const bool lightNeighborsReady = required_neighbors_have_data(record.coord, lightSignature, neighborhood);

        if (record.lightState == LightState::Ready &&
            lightNeighborsReady &&
            record.litAgainstSignature != 0 &&
            record.litAgainstSignature != lightSignature)
        {
            record.lightState = LightState::Stale;
        }

        if (lightJobsQueued < MaxLightJobsPerTick && _scheduler.should_light(record, lightNeighborsReady, lightSignature))
        {
            queue_light(chunk, lightSignature, neighborhood);
            ++lightJobsQueued;
            continue;
        }

        uint64_t meshSignature = 0;
        const bool meshNeighborsReady = required_neighbors_have_lighting(record.coord, meshSignature, neighborhood);

        if ((record.meshState == MeshState::Uploaded || record.meshState == MeshState::MeshReady) &&
            meshNeighborsReady &&
            record.meshedAgainstSignature != 0 &&
            record.meshedAgainstSignature != meshSignature)
        {
            record.meshState = MeshState::Stale;
        }

        if (meshJobsQueued < MaxMeshJobsPerTick && _scheduler.should_mesh(record, meshNeighborsReady, meshSignature))
        {
            queue_mesh(chunk, meshSignature, neighborhood);
            ++meshJobsQueued;
            continue;
        }

        if (_scheduler.should_upload(record))
        {
            record.uploadPending = true;
            record.meshState = MeshState::MeshReady;
            chunk->_state.store(ChunkState::Generated, std::memory_order::release);
            _renderReadyEvents.enqueue(ChunkRenderReadyEvent{
                .chunk = chunk,
                .generationId = record.chunkGenerationId,
                .neighborhoodSignature = record.meshedAgainstSignature,
                .data = record.data,
                .meshData = record.mesh
            });
        }
    }
}

void ChunkManager::reset_chunk_runtime(Chunk* chunk)
{
    if (chunk == nullptr)
    {
        return;
    }

    ChunkRuntime& runtime = _runtimeByChunk[chunk];
    runtime.record = ChunkRecord{
        .coord = chunk->_data->coord,
        .data = chunk->_data,
        .mesh = chunk->_meshData,
        .chunkGenerationId = chunk->_gen.load(std::memory_order::acquire),
        .dataVersion = 0,
        .lightVersion = 0,
        .litAgainstSignature = 0,
        .meshedAgainstSignature = 0,
        .uploadedSignature = 0,
        .residency = ChunkResidencyState::Resident,
        .dataState = DataState::Empty,
        .lightState = LightState::Missing,
        .meshState = MeshState::Missing,
        .generationJobInFlight = false,
        .lightJobInFlight = false,
        .meshJobInFlight = false,
        .uploadPending = false
    };
    chunk->_state.store(ChunkState::Uninitialized, std::memory_order::release);
}

void ChunkManager::mark_chunk_dirty(Chunk* chunk, const bool dataChanged, const bool lightingInvalidated)
{
    ChunkRuntime* const runtime = runtime_for(chunk);
    if (runtime == nullptr)
    {
        return;
    }

    ChunkRecord& record = runtime->record;
    if (record.data == nullptr || (record.dataState != DataState::Ready && record.dataState != DataState::Dirty))
    {
        return;
    }

    if (dataChanged)
    {
        record.dataVersion += 1;
        record.dataState = DataState::Dirty;
    }
    if (lightingInvalidated)
    {
        record.lightState = LightState::Stale;
    }
    record.meshState = MeshState::Stale;
    record.uploadPending = false;
    chunk->_state.store(ChunkState::Generated, std::memory_order::release);
}

void ChunkManager::queue_generate(Chunk* const chunk)
{
    ChunkRuntime* const runtime = runtime_for(chunk);
    if (runtime == nullptr)
    {
        return;
    }

    ChunkRecord& record = runtime->record;
    record.generationJobInFlight = true;
    record.dataState = DataState::Generating;
    const uint32_t generationId = record.chunkGenerationId;
    const ChunkCoord coord = record.coord;
    const glm::ivec2 position = record.data != nullptr ? record.data->position : glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE);

    _threadPool.post([this, chunk, generationId, coord, position]() noexcept
    {
        auto generated = std::make_shared<ChunkData>(coord, position);
        generated->generate();

        _generateResults.enqueue(ChunkGenerateResult{
            .chunk = chunk,
            .generationId = generationId,
            .data = generated
        });
    });
}

void ChunkManager::queue_light(Chunk* const chunk, const uint64_t neighborhoodSignature, const ChunkNeighborhood& neighborhood)
{
    ChunkRuntime* const runtime = runtime_for(chunk);
    if (runtime == nullptr)
    {
        return;
    }

    ChunkRecord& record = runtime->record;
    record.lightJobInFlight = true;
    record.lightState = LightState::Lighting;
    const uint32_t generationId = record.chunkGenerationId;
    const uint32_t dataVersion = record.dataVersion;

    _threadPool.post([this, chunk, generationId, dataVersion, neighborhoodSignature, neighborhood]() noexcept
    {
        auto litData = ChunkLighting::solve_skylight(neighborhood);

        _lightResults.enqueue(ChunkLightBuildResult{
            .chunk = chunk,
            .coord = neighborhood.center != nullptr ? neighborhood.center->coord : ChunkCoord{},
            .generationId = generationId,
            .dataVersion = dataVersion,
            .neighborhoodSignature = neighborhoodSignature,
            .litData = std::move(litData)
        });
    });
}

void ChunkManager::queue_mesh(Chunk* const chunk, const uint64_t neighborhoodSignature, const ChunkNeighborhood& neighborhood)
{
    ChunkRuntime* const runtime = runtime_for(chunk);
    if (runtime == nullptr)
    {
        return;
    }

    ChunkRecord& record = runtime->record;
    record.meshJobInFlight = true;
    record.meshState = MeshState::Meshing;
    const uint32_t generationId = record.chunkGenerationId;
    const uint32_t dataVersion = record.dataVersion;

    _threadPool.post([this, chunk, generationId, dataVersion, neighborhoodSignature, neighborhood]() noexcept
    {
        ChunkMesher mesher{ neighborhood, _ambientOcclusionEnabled };
        auto meshData = mesher.generate_mesh();

        _meshResults.enqueue(ChunkMeshBuildResult{
            .chunk = chunk,
            .coord = neighborhood.center != nullptr ? neighborhood.center->coord : ChunkCoord{},
            .generationId = generationId,
            .dataVersion = dataVersion,
            .neighborhoodSignature = neighborhoodSignature,
            .meshData = std::move(meshData)
        });
    });
}

bool ChunkManager::try_dequeue_render_reset(ChunkRenderResetEvent& event)
{
    return _renderResetEvents.try_dequeue(event);
}

bool ChunkManager::try_dequeue_render_ready(ChunkRenderReadyEvent& event)
{
    return _renderReadyEvents.try_dequeue(event);
}

std::optional<ChunkNeighborhood> ChunkManager::build_neighborhood(const ChunkCoord coord) const
{
    if (m_chunkCache == nullptr)
    {
        return std::nullopt;
    }

    Chunk* const centerChunk = m_chunkCache->get_chunk(coord);
    if (centerChunk == nullptr)
    {
        return std::nullopt;
    }

    const ChunkRuntime* const centerRuntime = runtime_for(centerChunk);
    if (centerRuntime == nullptr || centerRuntime->record.data == nullptr)
    {
        return std::nullopt;
    }

    ChunkNeighborhood neighborhood{};
    neighborhood.center = centerRuntime->record.data;

    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        const auto neighbor_chunk = m_chunkCache->get_chunk(offset_coord);
        if (neighbor_chunk == nullptr)
        {
            return std::nullopt;
        }

        const ChunkRuntime* const neighborRuntime = runtime_for(neighbor_chunk);
        if (neighborRuntime == nullptr ||
            neighborRuntime->record.data == nullptr ||
            (neighborRuntime->record.dataState != DataState::Ready && neighborRuntime->record.dataState != DataState::Dirty))
        {
            return std::nullopt;
        }
        switch (direction)
        {
        case NORTH:
            neighborhood.north = neighborRuntime->record.data;
            break;
        case SOUTH:
            neighborhood.south = neighborRuntime->record.data;
            break;
        case EAST:
            neighborhood.east = neighborRuntime->record.data;
            break;
        case WEST:
            neighborhood.west = neighborRuntime->record.data;
            break;
        case NORTH_EAST:
            neighborhood.northEast = neighborRuntime->record.data;
            break;
        case NORTH_WEST:
            neighborhood.northWest = neighborRuntime->record.data;
            break;
        case SOUTH_EAST:
            neighborhood.southEast = neighborRuntime->record.data;
            break;
        case SOUTH_WEST:
            neighborhood.southWest = neighborRuntime->record.data;
            break;
        }
    }

    return neighborhood;
}

std::optional<ChunkNeighborhood> ChunkManager::build_light_neighborhood(const ChunkCoord coord) const
{
    return build_neighborhood(coord);
}

ChunkManager::ChunkRuntime* ChunkManager::runtime_for(const Chunk* chunk)
{
    const auto it = _runtimeByChunk.find(const_cast<Chunk*>(chunk));
    return it != _runtimeByChunk.end() ? &it->second : nullptr;
}

const ChunkManager::ChunkRuntime* ChunkManager::runtime_for(const Chunk* chunk) const
{
    const auto it = _runtimeByChunk.find(const_cast<Chunk*>(chunk));
    return it != _runtimeByChunk.end() ? &it->second : nullptr;
}

uint64_t ChunkManager::compute_light_signature(const ChunkNeighborhood& neighborhood) const
{
    auto mix = [](const uint64_t seed, const uint64_t value)
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    };

    uint64_t signature = 0;
    if (const Chunk* const centerChunk = get_chunk(neighborhood.center->coord))
    {
        if (const ChunkRuntime* const centerRuntime = runtime_for(centerChunk))
        {
            signature = mix(signature, centerRuntime->record.dataVersion);
        }
    }

    for (const auto direction : directionList)
    {
        const ChunkData* const neighbor = neighborhood.get_by_offset(directionOffsetX[direction], directionOffsetZ[direction]);
        if (neighbor == nullptr)
        {
            return 0;
        }

        if (const Chunk* const chunk = get_chunk(neighbor->coord))
        {
            if (const ChunkRuntime* const runtime = runtime_for(chunk))
            {
                signature = mix(signature, runtime->record.dataVersion);
            }
        }
    }

    return signature;
}

uint64_t ChunkManager::compute_mesh_signature(const ChunkNeighborhood& neighborhood) const
{
    auto mix = [](const uint64_t seed, const uint64_t value)
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    };

    uint64_t signature = 0;
    if (const Chunk* const centerChunk = get_chunk(neighborhood.center->coord))
    {
        if (const ChunkRuntime* const centerRuntime = runtime_for(centerChunk))
        {
            signature = mix(signature, centerRuntime->record.dataVersion);
            signature = mix(signature, centerRuntime->record.lightVersion);
            signature = mix(signature, _ambientOcclusionEnabled ? 1ULL : 0ULL);
        }
    }

    for (const auto direction : directionList)
    {
        const ChunkData* const neighbor = neighborhood.get_by_offset(directionOffsetX[direction], directionOffsetZ[direction]);
        if (neighbor == nullptr)
        {
            return 0;
        }

        if (const Chunk* const chunk = get_chunk(neighbor->coord))
        {
            if (const ChunkRuntime* const runtime = runtime_for(chunk))
            {
                signature = mix(signature, runtime->record.dataVersion);
                signature = mix(signature, runtime->record.lightVersion);
            }
        }
    }

    return signature;
}

bool ChunkManager::required_neighbors_have_data(const ChunkCoord coord, uint64_t& signature, ChunkNeighborhood& neighborhood) const
{
    const auto builtNeighborhood = build_light_neighborhood(coord);
    if (!builtNeighborhood.has_value())
    {
        return false;
    }

    neighborhood = builtNeighborhood.value();
    signature = compute_light_signature(neighborhood);
    return signature != 0;
}

bool ChunkManager::required_neighbors_have_lighting(const ChunkCoord coord, uint64_t& signature, ChunkNeighborhood& neighborhood) const
{
    const auto builtNeighborhood = build_neighborhood(coord);
    if (!builtNeighborhood.has_value())
    {
        return false;
    }

    neighborhood = builtNeighborhood.value();
    const Chunk* const centerChunk = get_chunk(coord);
    const ChunkRuntime* const centerRuntime = runtime_for(centerChunk);
    if (centerRuntime == nullptr || centerRuntime->record.lightState != LightState::Ready)
    {
        return false;
    }

    for (const auto direction : directionList)
    {
        const ChunkCoord neighborCoord{
            coord.x + directionOffsetX[direction],
            coord.z + directionOffsetZ[direction]
        };
        const Chunk* const neighborChunk = get_chunk(neighborCoord);
        const ChunkRuntime* const neighborRuntime = runtime_for(neighborChunk);
        if (neighborRuntime == nullptr || neighborRuntime->record.lightState != LightState::Ready)
        {
            return false;
        }
    }

    signature = compute_mesh_signature(neighborhood);
    return signature != 0;
}
