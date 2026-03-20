
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
                const BlockEmissionDef emission = get_block_emission(block._type);
                if (emission.hasGlow)
                {
                    add_glow_to_mesh(x, y, z, emission, chunkMeshData->glowMesh);
                }
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

void ChunkMesher::add_glow_to_mesh(const int x, const int y, const int z, const BlockEmissionDef& emission, const std::shared_ptr<Mesh>& mesh) const
{
    const glm::vec3 center = glm::vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.58f, static_cast<float>(z) + 0.5f);
    const glm::vec3 color = glm::vec3(emission.color) / 255.0f;
    const float radius = emission.glowRadius;
    const float intensity = emission.glowIntensity;
    constexpr glm::vec2 corners[4] = {
        {-1.0f, -1.0f},
        { 1.0f, -1.0f},
        { 1.0f,  1.0f},
        {-1.0f,  1.0f}
    };

    for (int i = 0; i < 4; ++i)
    {
        mesh->_vertices.push_back({
            center,
            glm::vec3(corners[i], 0.0f),
            color,
            glm::vec2(radius, intensity),
            glm::vec3(0.0f)
        });
    }

    const uint32_t index = static_cast<uint32_t>(mesh->_vertices.size() - 4);
    mesh->_indices.push_back(index + 0);
    mesh->_indices.push_back(index + 1);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 3);
    mesh->_indices.push_back(index + 0);
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

    // Classic Minecraft-style voxel AO.
    // When both side neighbors are solid, the corner is fully occluded.
    if (edge1 && edge2)
    {
        return 0.55f;
    }

    const int occlusion = static_cast<int>(edge1) + static_cast<int>(edge2) + static_cast<int>(corner);
    switch (occlusion)
    {
    case 0:
        return 1.0f;
    case 1:
        return 0.82f;
    case 2:
        return 0.68f;
    default:
        return 0.55f;
    }
}

float ChunkMesher::calculate_vertex_skylight(const glm::ivec3 cubePos, const FaceDirection face, const int vertex) const
{
    const glm::ivec3 facePos = cubePos + glm::ivec3(faceOffsetX[face], faceOffsetY[face], faceOffsetZ[face]);
    const glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    const glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];
    const glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];

    const bool edge1Solid = is_position_solid(edge1Pos);
    const bool edge2Solid = is_position_solid(edge2Pos);
    const uint8_t faceLight = sample_sunlight(facePos);
    const uint8_t edge1Light = sample_sunlight(edge1Pos);
    const uint8_t edge2Light = sample_sunlight(edge2Pos);

    uint8_t cornerLight = 0;
    if (edge1Solid && edge2Solid)
    {
        // Minecraft-style corner rule: if both sides are blocked, reuse one of the
        // side samples instead of letting the diagonal leak excessive light.
        cornerLight = std::min(edge1Light, edge2Light);
    }
    else
    {
        cornerLight = sample_sunlight(cornerPos);
    }

    const float averageLight =
        (static_cast<float>(faceLight) +
         static_cast<float>(edge1Light) +
         static_cast<float>(edge2Light) +
         static_cast<float>(cornerLight)) * 0.25f;
    return averageLight / static_cast<float>(MAX_LIGHT_LEVEL);
}

glm::vec3 ChunkMesher::calculate_vertex_local_light(const glm::ivec3 cubePos, const FaceDirection face, const int vertex) const
{
    const glm::ivec3 facePos = cubePos + glm::ivec3(faceOffsetX[face], faceOffsetY[face], faceOffsetZ[face]);
    const glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    const glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];
    const glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];

    const bool edge1Solid = is_position_solid(edge1Pos);
    const bool edge2Solid = is_position_solid(edge2Pos);
    const glm::vec3 faceLight = sample_local_light(facePos);
    const glm::vec3 edge1Light = sample_local_light(edge1Pos);
    const glm::vec3 edge2Light = sample_local_light(edge2Pos);

    glm::vec3 cornerLight{0.0f};
    if (edge1Solid && edge2Solid)
    {
        cornerLight = glm::min(edge1Light, edge2Light);
    }
    else
    {
        cornerLight = sample_local_light(cornerPos);
    }

    return (faceLight + edge1Light + edge2Light + cornerLight) * 0.25f;
}

bool ChunkMesher::is_position_solid(const glm::ivec3& localPos) const
{
    const auto sample = sample_block(_neighborhood, localPos.x, localPos.y, localPos.z);
    if (sample.has_value())
    {
        return sample->block._solid;
    }

    return true;
}

uint8_t ChunkMesher::sample_sunlight(const glm::ivec3& localPos) const
{
    const auto sample = sample_block(_neighborhood, localPos.x, localPos.y, localPos.z);
    if (!sample.has_value() || sample->block._solid)
    {
        return 0;
    }

    return sample->block._sunlight;
}

glm::vec3 ChunkMesher::sample_local_light(const glm::ivec3& localPos) const
{
    const auto sample = sample_block(_neighborhood, localPos.x, localPos.y, localPos.z);
    if (!sample.has_value() || sample->block._solid)
    {
        return glm::vec3(0.0f);
    }

    return glm::vec3(
        static_cast<float>(sample->block._localLight.r),
        static_cast<float>(sample->block._localLight.g),
        static_cast<float>(sample->block._localLight.b)) / static_cast<float>(MAX_LIGHT_LEVEL);
}

//note: a block's position is the back-bottom-right of the cube.
void ChunkMesher::add_face_to_opaque_mesh(const int x, const int y, const int z, const FaceDirection face, const std::shared_ptr<Mesh>& mesh)
{
    const glm::ivec3 blockPos{x,y,z};
    const Block block = _neighborhood.center->blocks[x][y][z];
    glm::vec3 color = static_cast<glm::vec3>(blockColor[block._type]);

    if (block._type == BlockType::GROUND || block._type == BlockType::LEAVES)
    {
        const float heightFactor = std::clamp((static_cast<float>(y) - static_cast<float>(SEA_LEVEL)) / 96.0f, 0.0f, 1.0f);
        color *= glm::mix(0.97f, 1.04f, heightFactor);
    }

    for (int i = 0; i < 4; ++i) {
        glm::ivec3 position = blockPos + faceVertices[face][i];
        const float ao = _ambientOcclusionEnabled ? calculate_vertex_ao(blockPos, face, i) : 1.0f;
        const float sunLight = calculate_vertex_skylight(blockPos, face, i);
        const glm::vec3 localLight = calculate_vertex_local_light(blockPos, face, i);

        const auto normal = faceNormals[face];
        mesh->_vertices.push_back({ position, normal, color, glm::vec2(sunLight, ao), localLight });
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
    const auto baseColor = static_cast<glm::vec3>(blockColor[BlockType::WATER]);
    const auto normal = faceNormals[face];

    for (int i = 0; i < 4; ++i) {
        glm::ivec3 position = blockPos + faceVertices[face][i];
        const float skylight = calculate_vertex_skylight(blockPos, face, i);
        const glm::vec3 localLight = calculate_vertex_local_light(blockPos, face, i);

        mesh->_vertices.push_back({ position, normal, baseColor, glm::vec2(skylight, 1.0f), localLight });
    }

    uint32_t index = mesh->_vertices.size() - 4;
    mesh->_indices.push_back(index + 0);
    mesh->_indices.push_back(index + 1);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 3);
    mesh->_indices.push_back(index + 0);
}
