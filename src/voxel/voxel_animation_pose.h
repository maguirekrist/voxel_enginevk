#pragma once

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

struct VoxelAssemblyPosePart
{
    std::string partId{};
    std::optional<glm::vec3> localPosition{};
    std::optional<glm::quat> localRotation{};
    std::optional<glm::vec3> localScale{};
    std::optional<bool> visible{};
    std::optional<std::string> bindingStateId{};

    [[nodiscard]] bool operator==(const VoxelAssemblyPosePart& other) const = default;
};

class VoxelAssemblyPose
{
public:
    std::vector<VoxelAssemblyPosePart> parts{};

    [[nodiscard]] VoxelAssemblyPosePart* find_part(const std::string_view partId)
    {
        const auto it = std::ranges::find_if(parts, [&](const VoxelAssemblyPosePart& part)
        {
            return part.partId == partId;
        });
        return it != parts.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAssemblyPosePart* find_part(const std::string_view partId) const
    {
        const auto it = std::ranges::find_if(parts, [&](const VoxelAssemblyPosePart& part)
        {
            return part.partId == partId;
        });
        return it != parts.end() ? &(*it) : nullptr;
    }

    VoxelAssemblyPosePart& ensure_part(std::string_view partId)
    {
        if (VoxelAssemblyPosePart* existing = find_part(partId); existing != nullptr)
        {
            return *existing;
        }

        parts.push_back(VoxelAssemblyPosePart{ .partId = std::string(partId) });
        return parts.back();
    }

    void clear()
    {
        parts.clear();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return parts.empty();
    }
};
