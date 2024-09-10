#pragma once
#include <vk_types.h>
#include <vk_mesh.h>
#include <block.h>

constexpr unsigned int CHUNK_SIZE = 16;
constexpr unsigned int CHUNK_HEIGHT = 32;
constexpr unsigned int MAX_LIGHT_LEVEL = 15;

class Chunk {
public:
    Block _blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
    Mesh _mesh;
    glm::ivec2 _position;
    bool _isValid{ false };
    std::string _chunkKey;

    Block* get_block(const glm::ivec3& localPos);
    glm::ivec3 get_world_pos(const glm::ivec3& localPos);

    Chunk(const glm::ivec2& origin, const std::string key) : _position(origin), _chunkKey(key) {}

    static bool is_outside_chunk(const glm::ivec3& localPos);
};