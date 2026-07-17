#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <constants.h>
#include <format>
#include "block.h"
#include "decoration.h"
#include "world/structures/structure.h"
#include "render/mesh.h"

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

class ChunkBlocks
{
public:
    class ZSlice
    {
    public:
        explicit ZSlice(Block* base) noexcept : _base(base)
        {
        }

        [[nodiscard]] Block& operator[](const int z) noexcept
        {
            return _base[z];
        }

        [[nodiscard]] const Block& operator[](const int z) const noexcept
        {
            return _base[z];
        }

    private:
        Block* _base{};
    };

    class ConstZSlice
    {
    public:
        explicit ConstZSlice(const Block* base) noexcept : _base(base)
        {
        }

        [[nodiscard]] const Block& operator[](const int z) const noexcept
        {
            return _base[z];
        }

    private:
        const Block* _base{};
    };

    class YSlice
    {
    public:
        YSlice(Block* base, const int depth) noexcept : _base(base), _depth(depth)
        {
        }

        [[nodiscard]] ZSlice operator[](const int y) noexcept
        {
            return ZSlice{ _base + (static_cast<size_t>(y) * static_cast<size_t>(_depth)) };
        }

        [[nodiscard]] ConstZSlice operator[](const int y) const noexcept
        {
            return ConstZSlice{ _base + (static_cast<size_t>(y) * static_cast<size_t>(_depth)) };
        }

    private:
        Block* _base{};
        int _depth{};
    };

    class ConstYSlice
    {
    public:
        ConstYSlice(const Block* base, const int depth) noexcept : _base(base), _depth(depth)
        {
        }

        [[nodiscard]] ConstZSlice operator[](const int y) const noexcept
        {
            return ConstZSlice{ _base + (static_cast<size_t>(y) * static_cast<size_t>(_depth)) };
        }

    private:
        const Block* _base{};
        int _depth{};
    };

    ChunkBlocks() :
        ChunkBlocks(static_cast<int>(CHUNK_SIZE), static_cast<int>(CHUNK_HEIGHT), static_cast<int>(CHUNK_SIZE))
    {
    }

    ChunkBlocks(const int width, const int height, const int depth) :
        _width(width),
        _height(height),
        _depth(depth),
        _storage(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(depth))
    {
    }

    void resize(const int width, const int height, const int depth)
    {
        _width = width;
        _height = height;
        _depth = depth;
        _storage.assign(
            static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(depth),
            Block{});
    }

    [[nodiscard]] YSlice operator[](const int x) noexcept
    {
        return YSlice{ _storage.data() + x_plane_offset(x), _depth };
    }

    [[nodiscard]] ConstYSlice operator[](const int x) const noexcept
    {
        return ConstYSlice{ _storage.data() + x_plane_offset(x), _depth };
    }

    [[nodiscard]] Block& at(const int x, const int y, const int z) noexcept
    {
        return _storage[index(x, y, z)];
    }

    [[nodiscard]] const Block& at(const int x, const int y, const int z) const noexcept
    {
        return _storage[index(x, y, z)];
    }

    [[nodiscard]] int width() const noexcept
    {
        return _width;
    }

    [[nodiscard]] int height() const noexcept
    {
        return _height;
    }

    [[nodiscard]] int depth() const noexcept
    {
        return _depth;
    }

private:
    [[nodiscard]] size_t x_plane_offset(const int x) const noexcept
    {
        return static_cast<size_t>(x) * static_cast<size_t>(_height) * static_cast<size_t>(_depth);
    }

    [[nodiscard]] size_t index(const int x, const int y, const int z) const noexcept
    {
        return x_plane_offset(x) +
            (static_cast<size_t>(y) * static_cast<size_t>(_depth)) +
            static_cast<size_t>(z);
    }

    int _width{0};
    int _height{0};
    int _depth{0};
    std::vector<Block> _storage{};
};

enum class ChunkState : uint8_t
{
    Uninitialized = 0,
    Generated = 1,
    Rendered = 2
};

struct ChunkData
{
    enum class CachedPresenceState : int8_t
    {
        Unknown = -1,
        No = 0,
        Yes = 1
    };

    ChunkCoord coord{0, 0};
    glm::ivec2 position{0, 0};
    int voxelWidth{static_cast<int>(CHUNK_SIZE)};
    int voxelHeight{static_cast<int>(CHUNK_HEIGHT)};
    ChunkBlocks blocks{};
    std::shared_ptr<AppearanceBuffer> terrainAppearance{};
    std::vector<VoxelDecorationPlacement> voxelDecorations{};
    mutable std::atomic<CachedPresenceState> emissivePresence{CachedPresenceState::Unknown};

    ChunkData() = default;
    ChunkData(const ChunkData& other);
    ChunkData& operator=(const ChunkData& other);
    ChunkData(
        ChunkCoord chunkCoord,
        glm::ivec2 voxelOrigin,
        int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE),
        int chunkVoxelHeight = static_cast<int>(CHUNK_HEIGHT),
        bool allocateBlockStorage = true) :
        coord(chunkCoord),
        position(voxelOrigin),
        voxelWidth(chunkVoxelWidth),
        voxelHeight(chunkVoxelHeight),
        blocks(
            allocateBlockStorage ? chunkVoxelWidth : 0,
            allocateBlockStorage ? chunkVoxelHeight : 0,
            allocateBlockStorage ? chunkVoxelWidth : 0)
    {
    }

    void generate();
    void apply_structure_edits(std::span<const StructureBlockEdit> edits);
    void mark_emissive_blocks_present() noexcept;
    void invalidate_cached_properties() noexcept;
    [[nodiscard]] bool has_emissive_blocks() const;
    [[nodiscard]] bool has_block_storage() const noexcept;

    [[nodiscard]] bool contains_world_position(const glm::ivec3& worldPos) const;
    [[nodiscard]] glm::ivec3 to_local_position(const glm::ivec3& worldPos) const;
    [[nodiscard]] int voxel_depth() const noexcept { return voxelWidth; }
};

struct ChunkMeshData
{
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
    std::shared_ptr<Mesh> waterMesh = std::make_shared<Mesh>();
    std::shared_ptr<Mesh> glowMesh = std::make_shared<Mesh>();

    ~ChunkMeshData();
};

class Chunk {
public:
    std::shared_ptr<ChunkData> _data;
    std::shared_ptr<ChunkMeshData> _meshData;

    std::atomic_uint32_t _gen;
    std::atomic<ChunkState> _state = ChunkState::Uninitialized;

    explicit Chunk(
        ChunkCoord coord,
        int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE),
        int chunkVoxelHeight = static_cast<int>(CHUNK_HEIGHT));

    ~Chunk();

    glm::ivec3 get_world_pos(const glm::ivec3& localPos) const;
    void reset(
        ChunkCoord chunkCoord,
        int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE),
        int chunkVoxelHeight = static_cast<int>(CHUNK_HEIGHT));
    static std::optional<Direction> get_chunk_direction(
        const glm::ivec3& localPos,
        int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE));

    // static ChunkView to_view(const Chunk& chunk) noexcept
    // {
    //     return { chunk._blocks, chunk._position };
    // }

    static constexpr bool is_outside_chunk(
        const glm::ivec3& localPos,
        const int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE),
        const int chunkVoxelHeight = static_cast<int>(CHUNK_HEIGHT))
    {
        if (localPos.x < 0 || localPos.x >= chunkVoxelWidth || localPos.y < 0 || localPos.y >= chunkVoxelHeight || localPos.z < 0 || localPos.z >= chunkVoxelWidth) {
            return true;
        }

        return false;
    }
};
