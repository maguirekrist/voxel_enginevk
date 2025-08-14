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

constexpr std::array<ChunkCoord, 8> neighbors_of(const ChunkCoord c)
{
    std::array<ChunkCoord, 8> neighbors{};
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ c.x + offsetX, c.z + offsetZ };
        neighbors[direction] = offset_coord;
    }

    return neighbors;
}

using ChunkBlocks = Block[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];

enum class ChunkState : uint8_t
{
    Uninitialized = 0,
    Generated = 1,
    Rendered = 2
};

struct ChunkData
{
    ChunkCoord coord{0, 0};
    glm::ivec2 position{0, 0};
    ChunkBlocks blocks{};

    void generate();
};

struct ChunkMeshData
{
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
    std::shared_ptr<Mesh> waterMesh = std::make_shared<Mesh>();

    ~ChunkMeshData();
};

class Chunk {
public:
    std::shared_ptr<ChunkData> _data;
    std::shared_ptr<ChunkMeshData> _meshData;

    dev_collections::sparse_set<RenderObject>::Handle _opaqueHandle;
    dev_collections::sparse_set<RenderObject>::Handle _transparentHandle;
    std::atomic_uint32_t _gen;
    std::atomic<ChunkState> _state = ChunkState::Uninitialized;

    explicit Chunk(ChunkCoord coord);

    ~Chunk();

    glm::ivec3 get_world_pos(const glm::ivec3& localPos) const;
    void reset(ChunkCoord chunkCoord);

    // static ChunkView to_view(const Chunk& chunk) noexcept
    // {
    //     return { chunk._blocks, chunk._position };
    // }

    static constexpr bool is_outside_chunk(const glm::ivec3& localPos)
    {
        if (localPos.x < 0 || localPos.x >= CHUNK_SIZE || localPos.y < 0 || localPos.y >= CHUNK_HEIGHT || localPos.z < 0 || localPos.z >= CHUNK_SIZE) {
            return true;
        }

        return false;
    }
};