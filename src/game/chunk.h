#pragma once
#include <memory>
#include <vk_mesh.h>
#include <constants.h>
#include <format>
#include "block.h"
#include "render/render_primitives.h"


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
            size_t seed = std::hash<int>()(coord.x);
            seed ^= std::hash<int>()(coord.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
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

    dev_collections::sparse_set<RenderObject>::Handle _opaqueHandle;
    dev_collections::sparse_set<RenderObject>::Handle _transparentHandle;

    glm::ivec2 _position; //this is in world position, where is ChunkCoord is in chunk space.
    const ChunkCoord _chunkCoord;

    std::atomic<ChunkState> _state = ChunkState::Uninitialized;

    explicit Chunk(ChunkCoord coord);

    ~Chunk();

    glm::ivec3 get_world_pos(const glm::ivec3& localPos) const;
    void generate();

    static ChunkView to_view(const Chunk& chunk) noexcept
    {
        return { chunk._blocks, chunk._position };
    }

    static constexpr bool is_outside_chunk(const glm::ivec3& localPos)
    {
        if (localPos.x < 0 || localPos.x >= CHUNK_SIZE || localPos.y < 0 || localPos.y >= CHUNK_HEIGHT || localPos.z < 0 || localPos.z >= CHUNK_SIZE) {
            return true;
        }

        return false;
    }
};