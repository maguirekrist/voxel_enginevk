#include "chunk.h"
#include <world/terrain_gen.h>
#include <tracy/Tracy.hpp>
#include <render/mesh_release_queue.h>

ChunkData::ChunkData(const ChunkData& other) :
    coord(other.coord),
    position(other.position),
    voxelWidth(other.voxelWidth),
    voxelHeight(other.voxelHeight),
    blocks(other.blocks),
    terrainAppearance(other.terrainAppearance),
    voxelDecorations(other.voxelDecorations),
    emissivePresence(other.emissivePresence.load(std::memory_order_relaxed))
{
}

ChunkData& ChunkData::operator=(const ChunkData& other)
{
    if (this == &other)
    {
        return *this;
    }

    coord = other.coord;
    position = other.position;
    voxelWidth = other.voxelWidth;
    voxelHeight = other.voxelHeight;
    blocks = other.blocks;
    terrainAppearance = other.terrainAppearance;
    voxelDecorations = other.voxelDecorations;
    emissivePresence.store(other.emissivePresence.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

void Chunk::reset(const ChunkCoord chunkCoord, const int chunkVoxelWidth, const int chunkVoxelHeight)
{
    ZoneScopedN("Chunk::reset");
    _data = std::make_shared<ChunkData>(
        chunkCoord,
        glm::ivec2(chunkCoord.x * chunkVoxelWidth, chunkCoord.z * chunkVoxelWidth),
        chunkVoxelWidth,
        chunkVoxelHeight,
        false);
    _state = ChunkState::Uninitialized;
    _gen.fetch_add(1, std::memory_order_acq_rel);

    auto old_mesh_data = std::make_shared<ChunkMeshData>();
    _meshData.swap(old_mesh_data);
}

void ChunkData::generate()
{
    ZoneScopedN("Generate Chunk Data");
    emissivePresence.store(CachedPresenceState::No, std::memory_order_relaxed);
    const TerrainGenerator& terrainGenerator = TerrainGenerator::instance();
    WorldGenerationChunkResult generation{};
    {
        ZoneScopedN("ChunkData::GenerateChunkPipeline");
        generation = terrainGenerator.GenerateChunkPipeline(position.x, position.y);
    }
    {
        ZoneScopedN("ChunkData::RasterizeChunkTerrain");
        terrainGenerator.RasterizeChunkTerrain(generation, *this);
    }

    StructureGenerationContext structureContext{
        .chunkCoord = {coord.x, coord.z},
        .chunkOrigin = position,
        .terrainGenerator = &terrainGenerator,
        .terrainScaffold = &generation.columnScaffold,
        .terrainFeatures = &generation.featureInstances,
        .terrainAppearance = &generation.appearanceBuffer
    };
    std::vector<StructureBlockEdit> edits{};
    {
        ZoneScopedN("ChunkData::GenerateStructures");
        edits = StructureRegistry::instance().generate_overlapping(structureContext);
    }
    {
        ZoneScopedN("ChunkData::ApplyStructureEdits");
        apply_structure_edits(edits);
    }

    const DecorationGenerationContext decorationContext{
        .chunkOrigin = position,
        .terrainGenerator = &terrainGenerator,
        .terrainScaffold = &generation.columnScaffold,
        .terrainAppearance = &generation.appearanceBuffer,
        .chunkData = this
    };
    {
        ZoneScopedN("ChunkData::GenerateDecorations");
        voxelDecorations = DecorationRegistry::instance().generate_for_chunk(decorationContext);
    }
    {
        ZoneScopedN("ChunkData::CommitAppearanceBuffer");
        if (!generation.appearanceBuffer.voxels.empty())
        {
            terrainAppearance = std::make_shared<AppearanceBuffer>(std::move(generation.appearanceBuffer));
        }
        else
        {
            terrainAppearance.reset();
        }
    }
}

void ChunkData::apply_structure_edits(const std::span<const StructureBlockEdit> edits)
{
    ZoneScopedN("ChunkData::ApplyStructureEditsImpl");
    for (const StructureBlockEdit& edit : edits)
    {
        if (!contains_world_position(edit.worldPosition))
        {
            continue;
        }

        const glm::ivec3 localPos = to_local_position(edit.worldPosition);
        if (Chunk::is_outside_chunk(localPos, voxelWidth, voxelHeight))
        {
            continue;
        }

        const Block& previousBlock = blocks[localPos.x][localPos.y][localPos.z];
        const bool removedEmitter = get_block_emission(previousBlock._type).emits && !get_block_emission(edit.block._type).emits;
        blocks[localPos.x][localPos.y][localPos.z] = edit.block;
        if (get_block_emission(edit.block._type).emits)
        {
            mark_emissive_blocks_present();
        }
        else if (removedEmitter)
        {
            emissivePresence.store(CachedPresenceState::Unknown, std::memory_order_relaxed);
        }
    }
}

void ChunkData::mark_emissive_blocks_present() noexcept
{
    emissivePresence.store(CachedPresenceState::Yes, std::memory_order_relaxed);
}

void ChunkData::invalidate_cached_properties() noexcept
{
    emissivePresence.store(CachedPresenceState::Unknown, std::memory_order_relaxed);
}

bool ChunkData::has_emissive_blocks() const
{
    if (!has_block_storage())
    {
        emissivePresence.store(CachedPresenceState::No, std::memory_order_relaxed);
        return false;
    }

    const CachedPresenceState cachedPresence = emissivePresence.load(std::memory_order_relaxed);
    if (cachedPresence == CachedPresenceState::Yes)
    {
        return true;
    }

    if (cachedPresence == CachedPresenceState::No)
    {
        return false;
    }

    for (int x = 0; x < voxelWidth; ++x)
    {
        for (int z = 0; z < voxelWidth; ++z)
        {
            for (int y = 0; y < voxelHeight; ++y)
            {
                if (get_block_emission(blocks[x][y][z]._type).emits)
                {
                    emissivePresence.store(CachedPresenceState::Yes, std::memory_order_relaxed);
                    return true;
                }
            }
        }
    }

    emissivePresence.store(CachedPresenceState::No, std::memory_order_relaxed);
    return false;
}

bool ChunkData::has_block_storage() const noexcept
{
    return blocks.width() > 0 && blocks.height() > 0 && blocks.depth() > 0;
}

bool ChunkData::contains_world_position(const glm::ivec3& worldPos) const
{
    if (!has_block_storage())
    {
        return false;
    }

    return worldPos.x >= position.x &&
        worldPos.x < position.x + voxelWidth &&
        worldPos.z >= position.y &&
        worldPos.z < position.y + voxelWidth &&
        worldPos.y >= 0 &&
        worldPos.y < voxelHeight;
}

glm::ivec3 ChunkData::to_local_position(const glm::ivec3& worldPos) const
{
    return {
        worldPos.x - position.x,
        worldPos.y,
        worldPos.z - position.y
    };
}

ChunkMeshData::~ChunkMeshData()
{
    render::enqueue_mesh_release(std::move(mesh));
    render::enqueue_mesh_release(std::move(waterMesh));
    render::enqueue_mesh_release(std::move(glowMesh));
}

Chunk::Chunk(const ChunkCoord coord, const int chunkVoxelWidth, const int chunkVoxelHeight) :
    _data(std::make_shared<ChunkData>(
        coord,
        glm::ivec2(coord.x * chunkVoxelWidth, coord.z * chunkVoxelWidth),
        chunkVoxelWidth,
        chunkVoxelHeight,
        false)),
    _meshData(std::make_shared<ChunkMeshData>())
{
}

Chunk::~Chunk()
{
}

glm::ivec3 Chunk::get_world_pos(const glm::ivec3& localPos) const
{
    return { localPos.x + _data->position.x, localPos.y, localPos.z + _data->position.y };
}

std::optional<Direction> Chunk::get_chunk_direction(const glm::ivec3& localPos, const int chunkVoxelWidth)
{
    const auto x = localPos.x >= chunkVoxelWidth ? 1 : (localPos.x < 0 ? -1 : 0);
    const auto z = localPos.z >= chunkVoxelWidth ? 1 : (localPos.z < 0 ? -1 : 0);

    if (x == 0 && z == 1) {
        return NORTH;
    }
    if (x == 0 && z == -1) {
        return SOUTH;
    }
    if (x == -1 && z == 0) {
        return EAST;
    }
    if (x == 1 && z == 0) {
        return WEST;
    }
    if (x == -1 && z == 1) {
        return NORTH_EAST;
    }
    if (x == 1 && z == 1) {
        return NORTH_WEST;
    }
    if (x == -1 && z == -1) {
        return SOUTH_EAST;
    }
    if (x == 1 && z == -1) {
        return SOUTH_WEST;
    }

    return std::nullopt;
}
