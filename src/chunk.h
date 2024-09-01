#pragma once
#include <vk_types.h>
#include <vk_mesh.h>
#include <block.h>

constexpr unsigned int CHUNK_SIZE = 32;
constexpr unsigned int CHUNK_HEIGHT = 32;

class Chunk {
public:
    Block _blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
    Mesh _mesh;
    glm::ivec2 _origin;

    Chunk(glm::ivec2 origin) : _origin(origin) {}

    //Block get_block(int x, int y, int z);
    void generate_chunk_mesh();
    bool is_face_visible(const Block& block, FaceDirection face);
    bool is_outside_chunk(glm::ivec3 pos);
    void add_face_to_mesh(const Block& block, FaceDirection face);
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
};