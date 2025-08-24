#include "chunk.h"
#include <world/terrain_gen.h>
#include <tracy/Tracy.hpp>

#include "vk_engine.h"

void Chunk::reset(const ChunkCoord chunkCoord)
{
    _data = std::make_shared<ChunkData>(chunkCoord, glm::ivec2(chunkCoord.x * CHUNK_SIZE, chunkCoord.z * CHUNK_SIZE));
    _state = ChunkState::Uninitialized;
    _gen.fetch_add(1, std::memory_order_acq_rel);

    auto old_mesh_data = std::make_shared<ChunkMeshData>();
    _meshData.swap(old_mesh_data);

    VulkanEngine::instance()._opaqueSet.remove(_opaqueHandle);
    VulkanEngine::instance()._transparentSet.remove(_transparentHandle);
    // VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(old_mesh_data->mesh));
    // VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(old_mesh_data->waterMesh));

    _opaqueHandle = VulkanEngine::instance()._opaqueSet.insert(RenderObject{
        .mesh = _meshData->mesh,
        .material = VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
        .xzPos = glm::ivec2(_data->position.x, _data->position.y),
        .layer = RenderLayer::Opaque
    });
    _transparentHandle = VulkanEngine::instance()._transparentSet.insert(RenderObject{
        .mesh = _meshData->waterMesh,
        .material = VulkanEngine::instance()._materialManager.get_material("watermesh"),
        .xzPos = glm::ivec2(_data->position.x, _data->position.y),
        .layer = RenderLayer::Transparent
    });
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
                }
                else if(y <= SEA_LEVEL) {
                    block._solid = false;
                    block._type = BlockType::WATER;
                    int sunLight = std::max(-(static_cast<int>(SEA_LEVEL) - y) + static_cast<int>(MAX_LIGHT_LEVEL), 0);
                    block._sunlight = std::clamp(static_cast<uint8_t>(sunLight), static_cast<uint8_t>(1), static_cast<uint8_t>(MAX_LIGHT_LEVEL));
                }
                else {
                    block._solid = false;
                    block._type = BlockType::AIR;
                    block._sunlight = MAX_LIGHT_LEVEL;
                }
            }
        }
    }
}

ChunkMeshData::~ChunkMeshData()
{
    VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(mesh));
    VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(waterMesh));
}

Chunk::Chunk(const ChunkCoord coord) :
    _opaqueHandle(),
    _transparentHandle(),
    _data(std::make_shared<ChunkData>(coord, glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE))),
    _meshData(std::make_shared<ChunkMeshData>())
{
    _opaqueHandle = VulkanEngine::instance()._opaqueSet.insert(RenderObject{
        .mesh = _meshData->mesh,
        .material = VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
        .xzPos = glm::ivec2(_data->position.x, _data->position.y),
        .layer = RenderLayer::Opaque
    });
    _transparentHandle = VulkanEngine::instance()._transparentSet.insert(RenderObject{
        .mesh = _meshData->waterMesh,
        .material = VulkanEngine::instance()._materialManager.get_material("watermesh"),
        .xzPos = glm::ivec2(_data->position.x, _data->position.y),
        .layer = RenderLayer::Transparent
    });
}

Chunk::~Chunk()
{
    VulkanEngine::instance()._opaqueSet.remove(_opaqueHandle);
    VulkanEngine::instance()._transparentSet.remove(_transparentHandle);
}

glm::ivec3 Chunk::get_world_pos(const glm::ivec3& localPos) const
{
    return { localPos.x + _data->position.x, localPos.y, localPos.z + _data->position.y };
}


