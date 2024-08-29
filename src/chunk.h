#pragma once
#include <vk_types.h>
#include <vk_mesh.h>
#include <block.h>

constexpr unsigned int CHUNK_SIZE = 32;

class Chunk {
public:
    Block _blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    Mesh _mesh;

    //Block get_block(int x, int y, int z);
    void generate_chunk_mesh();
    bool is_face_visible(const Block& block, FaceDirection face);
    void add_face_to_mesh(const Block& block, FaceDirection face);
};