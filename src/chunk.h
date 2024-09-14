#pragma once
#include <vk_types.h>
#include <vk_mesh.h>
#include <block.h>

constexpr unsigned int CHUNK_SIZE = 16;
constexpr unsigned int CHUNK_HEIGHT = 256;
constexpr unsigned int MAX_LIGHT_LEVEL = 15;

struct ChunkCoord {
    int x, z;
    
    bool operator==(const ChunkCoord& other) const {
        return x == other.x && z == other.z;
    }
};

namespace std {
    template<>
    struct hash<ChunkCoord> {
        size_t operator()(const ChunkCoord& coord) const {
            return hash<int>()(coord.x) ^ (hash<int>()(coord.z) << 1);
        }
    };
}

class Chunk {
public:
    Block _blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
    Mesh _mesh;
    glm::ivec2 _position; //this is in world position, where is ChunkCoord is in chunk space.
    bool _isValid{ false };

    Block* get_block(const glm::ivec3& localPos);
    glm::ivec3 get_world_pos(const glm::ivec3& localPos);

    Chunk() {};

    Chunk(ChunkCoord coord) : _position(glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE)) {
        generate();
    }

    void reset(ChunkCoord newCoord);

    void generate();

    static bool is_outside_chunk(const glm::ivec3& localPos);
private:


};