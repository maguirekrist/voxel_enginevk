#pragma once
#include "terrain_gen.h"
#include <memory>
#include <vk_types.h>
#include <vk_mesh.h>
#include <block.h>
#include <constants.h>




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

enum Direction {
    NORTH,
    SOUTH,
    EAST,
    WEST,
    NORTH_EAST,
    NORTH_WEST,
    SOUTH_EAST,
    SOUTH_WEST
};

constexpr Direction directionList[8] = { NORTH, SOUTH, EAST, WEST, NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST };

constexpr int directionOffsetX[] = { 0, 0, -1, 1, -1, 1, -1, 1 };
constexpr int directionOffsetZ[] = { 1, -1, 0, 0, 1, 1, -1, -1 };



class Chunk {
public:
    Block _blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
    std::shared_ptr<Mesh> _mesh;
    glm::ivec2 _position; //this is in world position, where is ChunkCoord is in chunk space.
    bool _isValid{ false };

    Block* get_block(const glm::ivec3& localPos);
    glm::ivec3 get_world_pos(const glm::ivec3& localPos);

    Chunk() {
        _mesh = std::make_shared<Mesh>();
    };

    Chunk(ChunkCoord coord) : _position(glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE)) {
        _mesh = std::make_shared<Mesh>();
    }

    void reset(ChunkCoord newCoord);

    void generate(TerrainGenerator generator);

    static constexpr bool is_outside_chunk(const glm::ivec3& localPos)
    {
        if (localPos.x < 0 || localPos.x >= CHUNK_SIZE || localPos.y < 0 || localPos.y >= CHUNK_HEIGHT || localPos.z < 0 || localPos.z >= CHUNK_SIZE) {
            return true;
        }

        return false;
    }
private:


};