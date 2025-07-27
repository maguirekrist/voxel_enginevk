
#include <chunk_mesher.h>
#include "block.h"
#include "tracy/Tracy.hpp"
#include <world.h>

std::pair<std::unique_ptr<Mesh>, std::unique_ptr<Mesh>> ChunkMesher::generate_mesh()
{
    ZoneScopedN("Generate Chunk Mesh");
    auto mesh = std::make_unique<Mesh>();
    auto waterMesh = std::make_unique<Mesh>();

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                Block block = _chunk.blocks[x][y][z];
                if (block._solid) {
                    for(auto face : faceDirections)
                    {
                        if(is_face_visible(x, y, z, face))
                        {
                            add_face_to_opaque_mesh(x, y, z, face, mesh.get());
                        }
                    }
                } else if(block._type == BlockType::WATER)
                {
                    for(auto face : faceDirections)
                    {
                        if(is_face_visible_water(x, y, z, face))
                        {
                            add_face_to_water_mesh(x, y, z, face, waterMesh.get());
                        }
                    }
                }
            }
        }
    }

    return std::make_pair(std::move(mesh), std::move(waterMesh));
}

std::optional<const Block> ChunkMesher::get_face_neighbor(const int x, const int y, const int z, const FaceDirection face) const
{
    int nx = x + faceOffsetX[face];
    int ny = y + faceOffsetY[face];
    int nz = z + faceOffsetZ[face];

    if (Chunk::is_outside_chunk({ nx, ny, nz }))
    {
        //Perform more expensive search.
        auto direction = get_chunk_direction({ nx, ny, nz });
        if(direction.has_value())
        {
            auto chunk = _chunkNeighbors[direction.value()];
            auto local_pos = World::get_local_coordinates({ nx, ny, nz });
            return chunk.blocks[local_pos.x][local_pos.y][local_pos.z];
        }
        return std::nullopt;
    }

    return _chunk.blocks[nx][ny][nz];
}

bool ChunkMesher::is_face_visible(int x, int y, int z, FaceDirection face)
{
    // Check the neighboring block in the direction of 'face'
    int nx = x + faceOffsetX[face];
    int ny = y + faceOffsetY[face];
    int nz = z + faceOffsetZ[face];

    if (Chunk::is_outside_chunk({ nx, ny, nz }))
    {
        //Perform more expenive search.
        return !is_position_solid({ nx, ny, nz });
    }

    return !_chunk.blocks[nx][ny][nz]._solid;
}

bool ChunkMesher::is_face_visible_water(int x, int y, int z, FaceDirection face)
{
    auto faceBlock = get_face_neighbor(x, y, z, face).value();

    return faceBlock._type == BlockType::AIR;
}

//Face cube position of the 
float ChunkMesher::calculate_vertex_ao(const glm::ivec3 cubePos, const FaceDirection face, const int vertex)
{
    bool corner = false;
    bool edge1 = false;
    bool edge2 = false;

    glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];
    glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];

    if (!Chunk::is_outside_chunk(cornerPos)) {
        corner = _chunk.blocks[cornerPos.x][cornerPos.y][cornerPos.z]._solid;
    }
    else {
        corner = is_position_solid(cornerPos);
    }

    if (!Chunk::is_outside_chunk(edge1Pos))
    {
        edge1 = _chunk.blocks[edge1Pos.x][edge1Pos.y][edge1Pos.z]._solid;
    } else {
        edge1 = is_position_solid(edge1Pos);
    }

    if (!Chunk::is_outside_chunk(edge2Pos))
    {
        edge2 = _chunk.blocks[edge2Pos.x][edge2Pos.y][edge2Pos.z]._solid;
    } else {
        edge2 = is_position_solid(edge2Pos);
    }

    if (corner && edge1 && edge2)
    {
        return 0.15f;
    }
    else if (corner && (edge1 || edge2))
    {
        return 0.35f;
    }
    else if (edge1 && edge2)
    {
        return 0.35f;
    }
    else if (corner || edge1 || edge2)
    {
        return 0.35f;
    }

    return 1.0f;

}

void ChunkMesher::propagate_sunlight()
{
    std::queue<glm::ivec3> lightQueue;
    std::queue<glm::ivec3> crossChunkQueue;
    for (int x = 0; x < CHUNK_SIZE; x++)
    {
        for (int z = 0; z < CHUNK_SIZE; z++)
        {
            if (_chunk.blocks[x][CHUNK_HEIGHT - 1][z]._sunlight > 0)
            {
                lightQueue.push({ x, CHUNK_HEIGHT - 1, z});
            }
        }
    }

    // Directions for checking neighbors (UP, DOWN, LEFT, RIGHT, FRONT, BACK)
    while (!lightQueue.empty())
    {
        glm::ivec3 current = lightQueue.front();
        lightQueue.pop();

        for (auto face : faceDirections)
        {
            int nx = current.x + faceOffsetX[face];
            int ny = current.y + faceOffsetY[face];
            int nz = current.z + faceOffsetZ[face];

            Block neighbor;

            if(!Chunk::is_outside_chunk({ nx, ny, nz }))
            {
                neighbor = _chunk.blocks[nx][ny][nz];
                int newLight = _chunk.blocks[current.x][current.y][current.z]._sunlight - 1;

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

std::optional<Direction> ChunkMesher::get_chunk_direction(const glm::ivec3& localPos)
{
    //auto local_offset = World::get_local_coordinates(localPos);
    auto x = localPos.x > 15 ? 1 : (localPos.x < 0 ? -1 : 0);
    auto z = localPos.z > 15 ? 1 : (localPos.z < 0 ? -1 : 0);

    if (x == 0 && z == 1) {
        return NORTH;
    }
    else if (x == 0 && z == -1) {
        return SOUTH;
    }
    else if (x == 1 && z == 0) {
        return WEST;
    }
    else if (x == -1 && z == 0) {
        return EAST;
    }
    else if (x == 1 && z == 1) {
        return NORTH_WEST;
    }
    else if (x == -1 && z == 1) {
        return NORTH_EAST;
    }
    else if (x == 1 && z == -1) {
        return SOUTH_WEST;
    }
    else if (x == -1 && z == -1) {
        return SOUTH_EAST;
    }

    return std::nullopt;
}

bool ChunkMesher::is_position_solid(const glm::ivec3& localPos)
{
    auto direction = get_chunk_direction(localPos);
    if(direction.has_value()) {
        auto target_chunk = _chunkNeighbors[direction.value()];
        auto new_pos = World::get_local_coordinates(localPos);
        auto block = target_chunk.blocks[new_pos.x][new_pos.y][new_pos.z];

        return block._solid;
    }

    return true;
}

void ChunkMesher::propagate_pointlight(glm::vec3 lightPos, int lightLevel)
{
    throw std::runtime_error("Not implemented.");
}

//note: a block's position is the back-bottom-right of the cube.
void ChunkMesher::add_face_to_opaque_mesh(const int x, const int y, const int z, const FaceDirection face, Mesh* mesh)
{
    auto faceNeighbor = get_face_neighbor(x, y, z, face);
    float sunLight = faceNeighbor.has_value() ? static_cast<float>(faceNeighbor.value()._sunlight) / static_cast<float>(MAX_LIGHT_LEVEL) : 1.0f;

    glm::ivec3 blockPos{x,y,z};
    Block block = _chunk.blocks[x][y][z];
    glm::vec3 color = static_cast<glm::vec3>(blockColor[block._type]);

    for (int i = 0; i < 4; ++i) {
        //get the neighbors light-level
        glm::ivec3 position = blockPos + faceVertices[face][i];
        float ao = calculate_vertex_ao(blockPos, face, i);

        mesh->_vertices.push_back({ position, faceNormals[face], color * (ao * sunLight) });
    }

    // Add indices for the face (two triangles)
    uint32_t index = mesh->_vertices.size() - 4;
    mesh->_indices.push_back(index + 0);
    mesh->_indices.push_back(index + 1);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 2);
    mesh->_indices.push_back(index + 3);
    mesh->_indices.push_back(index + 0);
}

void ChunkMesher::add_face_to_water_mesh(const int x, const int y, const int z, const FaceDirection face, Mesh* mesh) const
{
    glm::ivec3 blockPos{x,y,z};
    Block block = _chunk.blocks[x][y][z];
    glm::vec3 color = static_cast<glm::vec3>(blockColor[block._type]);

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
