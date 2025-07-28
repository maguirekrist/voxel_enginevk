#pragma once
#include <memory>
#include <vk_mesh.h>
#include <constants.h>
#include <format>
#include "block.h"

struct ChunkCoord {
    int x, z;
    
    bool operator==(const ChunkCoord& other) const {
        return x == other.x && z == other.z;
    }
};

template <>
struct std::formatter<ChunkCoord> {
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ChunkCoord& coord, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "Chunk[x={}, z={}]", coord.x, coord.z);
    }
};

namespace std {
    template<>
    struct hash<ChunkCoord> {
        size_t operator()(const ChunkCoord& coord) const noexcept
        {
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

using ChunkBlocks = Block[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];

struct ChunkView
{
    const ChunkBlocks& blocks;
    const glm::ivec2 position;
    ChunkView(const ChunkBlocks& blocks, const glm::ivec2 position) : blocks(blocks), position(position) {}

    [[nodiscard]] std::optional<Block> get_block(const glm::ivec3& localPos) const;
    [[nodiscard]] glm::ivec3 get_world_pos(const glm::ivec3& localPos) const;
};

enum class ChunkState : uint8_t
{
    Uninitialized = 0,
    Generated = 1,
    Border = 2,
    Rendered = 3
};

class Chunk {
public:
    ChunkBlocks _blocks = {};
    std::shared_ptr<Mesh> _mesh;
    std::shared_ptr<Mesh> _waterMesh;
    glm::ivec2 _position; //this is in world position, where is ChunkCoord is in chunk space.
    const ChunkCoord _chunkCoord;

    std::atomic<ChunkState> _state = ChunkState::Uninitialized;

    explicit Chunk(const ChunkCoord coord) : _position(glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE)), _chunkCoord(coord) {
        _mesh = std::make_unique<Mesh>();
        _waterMesh = std::make_unique<Mesh>();
    };

    ~Chunk()
    {
    }

    glm::ivec3 get_world_pos(const glm::ivec3& localPos) const;
    void reset(ChunkCoord newCoord);
    void generate();

    static ChunkView to_view(const Chunk& chunk) noexcept
    {
        return ChunkView(chunk._blocks, chunk._position);
    }

    static constexpr bool is_outside_chunk(const glm::ivec3& localPos)
    {
        if (localPos.x < 0 || localPos.x >= CHUNK_SIZE || localPos.y < 0 || localPos.y >= CHUNK_HEIGHT || localPos.z < 0 || localPos.z >= CHUNK_SIZE) {
            return true;
        }

        return false;
    }
};