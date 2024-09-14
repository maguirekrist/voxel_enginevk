
#include <chunk_mesher.h>

void ChunkMesher::generate_mesh(Chunk* chunk)
{
    fmt::println("Generating mesh for chunk");

    _chunk = chunk;
    //propagate_sunlight();

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                Block& block = _chunk->_blocks[x][y][z];
                if (block._solid) {
                    for(auto face : faceDirections)
                    {
                        if(is_face_visible(block, face))
                        {
                            add_face_to_mesh(block, face);
                        }
                    }
                }
            }
        }
    }

    _chunk->_mesh = _mesh;

    fmt::println("Finished generating chunk");
}

Block* ChunkMesher::get_face_neighbor(const Block& block, FaceDirection face)
{
    int nx = block._position.x + faceOffsetX[face];
    int ny = block._position.y + faceOffsetY[face];
    int nz = block._position.z + faceOffsetZ[face];

    if (Chunk::is_outside_chunk({ nx, ny, nz }))
    {
        //Perform more expenive search.
        return _manager->getBlockGlobal(_chunk->get_world_pos({ nx, ny, nz }));
    }

    return &_chunk->_blocks[nx][ny][nz];
}

bool ChunkMesher::is_face_visible(const Block& block, FaceDirection face)
{
    // Check the neighboring block in the direction of 'face'
    int nx = block._position.x + faceOffsetX[face];
    int ny = block._position.y + faceOffsetY[face];
    int nz = block._position.z + faceOffsetZ[face];

    if (Chunk::is_outside_chunk({ nx, ny, nz }))
    {
        //Perform more expenive search.
        return !is_position_solid({ nx, ny, nz });
    }

    return !_chunk->_blocks[nx][ny][nz]._solid;
}

//Face cube position of the 
float ChunkMesher::calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex)
{
    bool corner = false;
    bool edge1 = false;
    bool edge2 = false;

    glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];
    glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];

    if (!Chunk::is_outside_chunk(cornerPos)) {
        corner = _chunk->_blocks[cornerPos.x][cornerPos.y][cornerPos.z]._solid;
    }
    else {
        corner = is_position_solid(cornerPos);
    }

    if (!Chunk::is_outside_chunk(edge1Pos))
    {
        edge1 = _chunk->_blocks[edge1Pos.x][edge1Pos.y][edge1Pos.z]._solid;
    } else {
        edge1 = is_position_solid(edge1Pos);
    }

    if (!Chunk::is_outside_chunk(edge2Pos))
    {
        edge2 = _chunk->_blocks[edge2Pos.x][edge2Pos.y][edge2Pos.z]._solid;
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
            if (_chunk->_blocks[x][CHUNK_HEIGHT - 1][z]._sunlight > 0)
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

            Block* neighbor;

            if(!Chunk::is_outside_chunk({ nx, ny, nz }))
            {
                neighbor = &_chunk->_blocks[nx][ny][nz];
                int newLight = _chunk->_blocks[current.x][current.y][current.z]._sunlight - 1;

                if (neighbor && !neighbor->_solid && neighbor->_sunlight < newLight)
                {
                    neighbor->_sunlight = newLight;
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

bool ChunkMesher::is_position_solid(glm::ivec3 localPos)
{
    auto block = _manager->getBlockGlobal(_chunk->get_world_pos(localPos));
    if(block != nullptr)
    {
        return block->_solid;
    } else {
        return false;
    }
}

void ChunkMesher::propagate_pointlight(glm::vec3 lightPos, int lightLevel)
{
    throw std::runtime_error("Not implemented.");
}

//note: a block's position is the back-bottom-right of the cube.
void ChunkMesher::add_face_to_mesh(const Block& block, FaceDirection face)
{
    Block* faceNeighbor = get_face_neighbor(block, face);
    float sunLight = faceNeighbor ? static_cast<float>(faceNeighbor->_sunlight) / static_cast<float>(MAX_LIGHT_LEVEL) : MAX_LIGHT_LEVEL;

    for (int i = 0; i < 4; ++i) {
        //get the neighbors light-level
        glm::vec3 position = block._position + faceVertices[face][i];
        float ao = calculate_vertex_ao(block._position, face, i);
        
        //TODO: figure out how AO effects the color of the block/vertex. You can use vertex interpolation to shade.
        _mesh._vertices.push_back({ position, glm::vec3(0.0f), faceColors[face] * (ao) });
    }

    // Add indices for the face (two triangles)
    uint32_t index = _mesh._vertices.size() - 4;
    _mesh._indices.push_back(index + 0);
    _mesh._indices.push_back(index + 1);
    _mesh._indices.push_back(index + 2);
    _mesh._indices.push_back(index + 2);
    _mesh._indices.push_back(index + 3);
    _mesh._indices.push_back(index + 0);
}