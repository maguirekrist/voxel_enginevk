
#pragma once
#include <vk_types.h>
#include <random.h>
#include <chunk.h>
#include <FastNoise/FastNoise.h>

enum WorldDirection {
    NORTH,
    SOUTH,
    EAST,
    WEST
};

constexpr WorldDirection worldDirections[4] = { WorldDirection::NORTH, WorldDirection::SOUTH, WorldDirection::EAST, WorldDirection::WEST };

constexpr glm::ivec2 worldDirectionOffset[4] = {
    { 0, 1 },
    { 0, -1 },
    { -1, 0},
    { 1, 0 }
};

class World {
public:
    std::unordered_map<std::string, std::unique_ptr<Chunk>> _chunkMap;
    //std::vector<std::unique_ptr<Chunk>> _chunks;
    int _seed;
    FastNoise::GeneratorSource _generator;

    World();

    bool is_position_solid(const glm::ivec3& worldPos);
    Block* get_block(const glm::ivec3& worldPos);
    Chunk* get_chunk(glm::vec3 worldPos); 
    void generate_chunk(int xStart, int yStart);

    static glm::ivec2 get_chunk_coordinates(const glm::vec3& worldPos);
    static glm::ivec2 get_chunk_origin(const glm::vec3& worldPos);
    static glm::ivec3 get_local_coordinates(const glm::vec3& worldPos);

private:
    void update_chunk(Chunk& chunk, std::vector<float>& heightMap);
    std::string get_chunk_key(const glm::ivec2& worldPos);
};