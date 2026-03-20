#include "chunk.h"
#include <world/terrain_gen.h>
#include <tracy/Tracy.hpp>
#include <render/mesh_release_queue.h>

void Chunk::reset(const ChunkCoord chunkCoord)
{
    _data = std::make_shared<ChunkData>(chunkCoord, glm::ivec2(chunkCoord.x * CHUNK_SIZE, chunkCoord.z * CHUNK_SIZE));
    _state = ChunkState::Uninitialized;
    _gen.fetch_add(1, std::memory_order_acq_rel);

    auto old_mesh_data = std::make_shared<ChunkMeshData>();
    _meshData.swap(old_mesh_data);
}

void ChunkData::generate()
{
    ZoneScopedN("Generate Chunk Data");
    std::vector<float> chunkHeightMap = TerrainGenerator::instance().GenerateHeightMap(position.x, position.y);
    for(int x = 0; x < CHUNK_SIZE; x++)
    {
        for(int z = 0; z < CHUNK_SIZE; z++)
        {
            // auto height = generator.NormalizeHeight(chunkHeightMap, CHUNK_HEIGHT, CHUNK_SIZE, x, z);
            for (int y = 0; y < CHUNK_HEIGHT; y++)
            {
                Block& block = blocks[x][y][z];
                if (y <= chunkHeightMap[(z * CHUNK_SIZE) + x])
                {
                    block._solid = true;
                    block._type = BlockType::GROUND;
                    block._sunlight = 0;
                    block._localLightR = 0;
                    block._localLightG = 0;
                    block._localLightB = 0;
                }
                else if(y <= SEA_LEVEL) {
                    block._solid = false;
                    block._type = BlockType::WATER;
                    block._sunlight = 0;
                    block._localLightR = 0;
                    block._localLightG = 0;
                    block._localLightB = 0;
                }
                else {
                    block._solid = false;
                    block._type = BlockType::AIR;
                    block._sunlight = 0;
                    block._localLightR = 0;
                    block._localLightG = 0;
                    block._localLightB = 0;
                }
            }
        }
    }

    StructureGenerationContext structureContext{
        .chunkCoord = {coord.x, coord.z},
        .chunkOrigin = position
    };
    const std::vector<StructureBlockEdit> edits = StructureRegistry::instance().generate_overlapping(structureContext);
    apply_structure_edits(edits);
}

void ChunkData::apply_structure_edits(const std::span<const StructureBlockEdit> edits)
{
    for (const StructureBlockEdit& edit : edits)
    {
        if (!contains_world_position(edit.worldPosition))
        {
            continue;
        }

        const glm::ivec3 localPos = to_local_position(edit.worldPosition);
        if (Chunk::is_outside_chunk(localPos))
        {
            continue;
        }

        blocks[localPos.x][localPos.y][localPos.z] = edit.block;
    }
}

bool ChunkData::contains_world_position(const glm::ivec3& worldPos) const
{
    return worldPos.x >= position.x &&
        worldPos.x < position.x + static_cast<int>(CHUNK_SIZE) &&
        worldPos.z >= position.y &&
        worldPos.z < position.y + static_cast<int>(CHUNK_SIZE) &&
        worldPos.y >= 0 &&
        worldPos.y < static_cast<int>(CHUNK_HEIGHT);
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
}

Chunk::Chunk(const ChunkCoord coord) :
    _data(std::make_shared<ChunkData>(coord, glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE))),
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

std::optional<Direction> Chunk::get_chunk_direction(const glm::ivec3& localPos)
{
    const auto x = localPos.x >= CHUNK_SIZE ? 1 : (localPos.x < 0 ? -1 : 0);
    const auto z = localPos.z >= CHUNK_SIZE ? 1 : (localPos.z < 0 ? -1 : 0);

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
