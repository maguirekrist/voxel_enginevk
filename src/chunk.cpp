#include "chunk.h"

void Chunk::generate_chunk_mesh()
{
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                Block& block = _blocks[x][y][z];
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
}

bool Chunk::is_face_visible(const Block& block, FaceDirection face)
{
    // Check the neighboring block in the direction of 'face'
    int nx = block._position.x + faceOffsetX[face];
    int ny = block._position.y + faceOffsetY[face];
    int nz = block._position.z + faceOffsetZ[face];

    // If the neighboring block is outside the chunk, assume it's air (or check adjacent chunk)
    if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) {
        return true;
    }

    return !_blocks[nx][ny][nz]._solid;
}

bool Chunk::is_outside_chunk(glm::ivec3 pos)
{
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) {
        return true;
    }

    return false;
}

//Face cube position of the 
float Chunk::calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex)
{
    bool corner = false;
    bool edge1 = false;
    bool edge2 = false;

    glm::ivec3 cornerPos = cubePos + CornerOffsets[face][vertex];
    glm::ivec3 edge1Pos = cubePos + Side1Offsets[face][vertex];
    glm::ivec3 edge2Pos = cubePos + Side2Offsets[face][vertex];

    if (!is_outside_chunk(cornerPos)) {
        corner = _blocks[cornerPos.x][cornerPos.y][cornerPos.z]._solid;
    }

    if (!is_outside_chunk(edge1Pos))
    {
        edge1 = _blocks[edge1Pos.x][edge1Pos.y][edge1Pos.z]._solid;
    }

    if (!is_outside_chunk(edge2Pos))
    {
        edge2 = _blocks[edge2Pos.x][edge2Pos.y][edge2Pos.z]._solid;
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

//note: a block's position is the back-bottom-right of the cube.
void Chunk::add_face_to_mesh(const Block& block, FaceDirection face)
{
    for (int i = 0; i < 4; ++i) {
        glm::vec3 position = block._position + faceVertices[face][i];
        float ao = calculate_vertex_ao(block._position, face, i);
        //TODO: figure out how AO effects the color of the block/vertex. You can use vertex interpolation to shade.
        _mesh._vertices.push_back({ position, glm::vec3(0.0f), faceColors[face] * ao });
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
