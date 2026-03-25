#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct VoxelColor
{
    uint8_t r{255};
    uint8_t g{255};
    uint8_t b{255};
    uint8_t a{255};

    [[nodiscard]] glm::vec3 to_vec3() const
    {
        return glm::vec3(
            static_cast<float>(r),
            static_cast<float>(g),
            static_cast<float>(b)) / 255.0f;
    }

    [[nodiscard]] glm::vec4 to_vec4() const
    {
        return glm::vec4(to_vec3(), static_cast<float>(a) / 255.0f);
    }

    auto operator<=>(const VoxelColor&) const = default;
};

struct VoxelCoord
{
    int x{0};
    int y{0};
    int z{0};

    auto operator<=>(const VoxelCoord&) const = default;
};

struct VoxelCoordHash
{
    size_t operator()(const VoxelCoord& coord) const noexcept
    {
        size_t seed = static_cast<size_t>(coord.x) * 73856093u;
        seed ^= static_cast<size_t>(coord.y) * 19349663u;
        seed ^= static_cast<size_t>(coord.z) * 83492791u;
        return seed;
    }
};

struct VoxelBounds
{
    bool valid{false};
    VoxelCoord min{};
    VoxelCoord max{};

    [[nodiscard]] glm::ivec3 dimensions() const
    {
        if (!valid)
        {
            return glm::ivec3(0);
        }

        return glm::ivec3(
            (max.x - min.x) + 1,
            (max.y - min.y) + 1,
            (max.z - min.z) + 1);
    }

    [[nodiscard]] glm::vec3 center() const
    {
        if (!valid)
        {
            return glm::vec3(0.0f);
        }

        return glm::vec3(
            (static_cast<float>(min.x + max.x) * 0.5f) + 0.5f,
            (static_cast<float>(min.y + max.y) * 0.5f) + 0.5f,
            (static_cast<float>(min.z + max.z) * 0.5f) + 0.5f);
    }
};

struct VoxelAttachment
{
    std::string name{};
    glm::vec3 position{0.0f};
    glm::vec3 forward{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    auto operator<=>(const VoxelAttachment&) const = default;
};

class VoxelModel
{
public:
    using Storage = std::unordered_map<VoxelCoord, VoxelColor, VoxelCoordHash>;

    std::string assetId{"untitled"};
    std::string displayName{"Untitled"};
    float voxelSize{1.0f / 16.0f};
    glm::vec3 pivot{0.0f, 0.0f, 0.0f};
    std::vector<VoxelAttachment> attachments{};

    [[nodiscard]] bool contains(const VoxelCoord& coord) const
    {
        return _voxels.contains(coord);
    }

    [[nodiscard]] const VoxelColor* try_get(const VoxelCoord& coord) const
    {
        if (const auto it = _voxels.find(coord); it != _voxels.end())
        {
            return &it->second;
        }

        return nullptr;
    }

    void set_voxel(const VoxelCoord& coord, const VoxelColor& color)
    {
        _voxels.insert_or_assign(coord, color);
    }

    bool remove_voxel(const VoxelCoord& coord)
    {
        return _voxels.erase(coord) > 0;
    }

    void clear()
    {
        _voxels.clear();
    }

    [[nodiscard]] size_t voxel_count() const noexcept
    {
        return _voxels.size();
    }

    [[nodiscard]] const Storage& voxels() const noexcept
    {
        return _voxels;
    }

    [[nodiscard]] VoxelBounds bounds() const
    {
        VoxelBounds result{};
        if (_voxels.empty())
        {
            return result;
        }

        result.valid = true;
        result.min = VoxelCoord{
            .x = std::numeric_limits<int>::max(),
            .y = std::numeric_limits<int>::max(),
            .z = std::numeric_limits<int>::max()
        };
        result.max = VoxelCoord{
            .x = std::numeric_limits<int>::min(),
            .y = std::numeric_limits<int>::min(),
            .z = std::numeric_limits<int>::min()
        };

        for (const auto& [coord, color] : _voxels)
        {
            (void)color;
            result.min.x = std::min(result.min.x, coord.x);
            result.min.y = std::min(result.min.y, coord.y);
            result.min.z = std::min(result.min.z, coord.z);
            result.max.x = std::max(result.max.x, coord.x);
            result.max.y = std::max(result.max.y, coord.y);
            result.max.z = std::max(result.max.z, coord.z);
        }

        return result;
    }

    [[nodiscard]] const VoxelAttachment* find_attachment(const std::string_view name) const
    {
        const auto it = std::ranges::find_if(attachments, [&](const VoxelAttachment& attachment)
        {
            return attachment.name == name;
        });
        return it != attachments.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] bool operator==(const VoxelModel& other) const = default;

private:
    Storage _voxels{};
};
