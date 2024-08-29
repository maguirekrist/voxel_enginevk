#include "chunk.h"

void Chunk::generate_chunk_mesh()
{
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
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

void Chunk::add_face_to_mesh(const Block& block, FaceDirection face)
{
    for (int i = 0; i < 4; ++i) {
        glm::vec3 position = block._position + faceVertices[face][i];
        //glm::vec3 color = calculateVertexColor(chunk, x, y, z, face, i); // AO could influence this
        _mesh._vertices.push_back({ position, glm::vec3(0.0f), block._color });
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
