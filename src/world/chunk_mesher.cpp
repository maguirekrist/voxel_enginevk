
#include "chunk_mesher.h"
#include "../game/block.h"
#include "tracy/Tracy.hpp"
#include <game/world.h>

std::shared_ptr<ChunkMeshData> ChunkMesher::generate_mesh()
{
    ZoneScopedN("Generate Chunk Mesh");

    auto chunkMeshData = std::make_shared<ChunkMeshData>();
    const auto& chunk = _neighborhood.center;
    if (chunk == nullptr)
    {
        return chunkMeshData;
    }

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const Block block = chunk->blocks[x][y][z];
                if (block._solid) {
                    for(const auto face : faceDirections)
                    {
                        if(is_face_visible(x, y, z, face))
                        {
                            add_face_to_opaque_mesh(x, y, z, face, chunkMeshData->mesh);
                        }
                    }
                } else if(block._type == BlockType::WATER)
                {
                    for(const auto face : faceDirections)
                    {
                        if(is_face_visible_water(x, y, z, face))
                        {
                            add_face_to_water_mesh(x, y, z, face, chunkMeshData->waterMesh);
                        }
                    }
                }
            }
        }
    }

    return chunkMeshData;
}

std::optional<const Block> ChunkMesher::get_face_neighbor(const int x, const int y, const int z, const FaceDirection face) const
{
    const auto sample = sample_block(_neighborhood, x + faceOffsetX[face], y + faceOffsetY[face], z + faceOffsetZ[face]);
    if (!sample.has_value())
    {
        return std::nullopt;
    }

    return sample->block;
}

bool ChunkMesher::is_face_visible(int x, int y, int z, FaceDirection face)
{
    int nx = x + faceOffsetX[face];
    int ny = y + faceOffsetY[face];
    int nz = z + faceOffsetZ[face];

    return !is_position_solid({ nx, ny, nz });
}

bool ChunkMesher::is_face_visible_water(int x, int y, int z, FaceDirection face)
{
    auto faceBlock = get_face_neighbor(x, y, z, face);

    if (faceBlock.has_value())
    {
        return faceBlock.value()._type == BlockType::AIR;
    } else
    {
        return false;
    }
}

//Face cube position of the 
float ChunkMesher::calculate_vertex_ao(const glm::ivec3 cubePos, const FaceDirection face, const int vertex)
{
    glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];
    glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];

    const bool corner = is_position_solid(cornerPos);
    const bool edge1 = is_position_solid(edge1Pos);
    const bool edge2 = is_position_solid(edge2Pos);

    // Classic voxel AO uses 4 levels based on two side blockers and one corner blocker.
    // The previous weights were much harsher and crushed corners too aggressively.
    if (edge1 && edge2)
    {
        return GameConfig::AO_HEAVY;
    }

    const int occlusion = static_cast<int>(edge1) + static_cast<int>(edge2) + static_cast<int>(corner);
    switch (occlusion)
    {
    case 0:
        return GameConfig::AO_FULL_LIGHT;
    case 1:
        return GameConfig::AO_LIGHT;
    case 2:
        return GameConfig::AO_MEDIUM;
    default:
        return GameConfig::AO_HEAVY;
    }
}

void ChunkMesher::propagate_sunlight()
{
    std::queue<glm::ivec3> lightQueue;
    std::queue<glm::ivec3> crossChunkQueue;
    for (int x = 0; x < CHUNK_SIZE; x++)
    {
        for (int z = 0; z < CHUNK_SIZE; z++)
        {
            if (_neighborhood.center->blocks[x][CHUNK_HEIGHT - 1][z]._sunlight > 0)
            {
                lightQueue.push({ x, CHUNK_HEIGHT - 1, z});
            }
        }
    }

    // Directions for checking neighbors (UP, DOWN, LEFT, RIGHT, FRONT, BACK)
    while (!lightQueue.empty())
    {
        const glm::ivec3 current = lightQueue.front();
        lightQueue.pop();

        for (auto face : faceDirections)
        {
            int nx = current.x + faceOffsetX[face];
            int ny = current.y + faceOffsetY[face];
            int nz = current.z + faceOffsetZ[face];

            if(!Chunk::is_outside_chunk({ nx, ny, nz }))
            {
                Block neighbor = _neighborhood.center->blocks[nx][ny][nz];
                const int newLight = _neighborhood.center->blocks[current.x][current.y][current.z]._sunlight - 1;

                if (!neighbor._solid && neighbor._sunlight < newLight)
                {
                    neighbor._sunlight = newLight;
                    lightQueue.push({ nx, ny, nz });
                }
            } else {
                // neighbor = _world.get_block(_chunk->get_world_pos({ nx, ny, nz }));
                // int newLight = _chunk->_blocks[current.x][current.y][current.z]._sunlight - 1;
                // if (neighbor && !neighbor->_solid && neighbor->_sunlight < newLight)
                // {
                //     neighbor->_sunlight = newLight;
                //     crossChunkQueue.push({ nx, ny, nz });
                // }
            }
        }
    }

    while (!crossChunkQueue.empty())
    {
        glm::ivec3 current = crossChunkQueue.front();
        crossChunkQueue.pop();
    }
}

bool ChunkMesher::is_position_solid(const glm::ivec3& localPos)
{
    const auto sample = sample_block(_neighborhood, localPos.x, localPos.y, localPos.z);
    if (sample.has_value())
    {
        return sample->block._solid;
    }

    return true;
}

void ChunkMesher::propagate_pointlight(glm::vec3 lightPos, int lightLevel)
{
    throw std::runtime_error("Not implemented.");
}

//note: a block's position is the back-bottom-right of the cube.
void ChunkMesher::add_face_to_opaque_mesh(const int x, const int y, const int z, const FaceDirection face, const std::shared_ptr<Mesh>& mesh)
{
    const auto faceNeighbor = get_face_neighbor(x, y, z, face);
    const float sunLight = faceNeighbor.has_value() ? static_cast<float>(faceNeighbor.value()._sunlight) / static_cast<float>(MAX_LIGHT_LEVEL) : 1.0f;

    const glm::ivec3 blockPos{x,y,z};
    const Block block = _neighborhood.center->blocks[x][y][z];
    const auto color = static_cast<glm::vec3>(blockColor[block._type]);

    for (int i = 0; i < 4; ++i) {
        //get the neighbors light-level
        glm::ivec3 position = blockPos + faceVertices[face][i];
        const float ao = calculate_vertex_ao(blockPos, face, i);

        const auto normal = faceNormals[face];
        const auto final_color = color * (ao * sunLight);

        mesh->_vertices.push_back({ position, normal, final_color });
    }

    // Add indices for the face (two triangles)
    const uint32_t index = mesh->_vertices.size() - 4;
    mesh->_indices.push_back(index + 0);
    mesh->_indices.push_back(index + 1);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 3);
    mesh->_indices.push_back(index + 0);
}

void ChunkMesher::add_face_to_water_mesh(const int x, const int y, const int z, const FaceDirection face, const std::shared_ptr<Mesh>& mesh) const
{
    const glm::ivec3 blockPos{x,y,z};
    const Block block = _neighborhood.center->blocks[x][y][z];
    const auto color = static_cast<glm::vec3>(blockColor[block._type]);

    for (int i = 0; i < 4; ++i) {
        glm::ivec3 position = blockPos + faceVertices[face][i];
    
        mesh->_vertices.push_back({ position, faceNormals[face], color });
    }

    uint32_t index = mesh->_vertices.size() - 4;
    mesh->_indices.push_back(index + 0);
    mesh->_indices.push_back(index + 1);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 3);
    mesh->_indices.push_back(index + 0);
}
