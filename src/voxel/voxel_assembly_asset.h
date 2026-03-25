#pragma once

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

struct VoxelAssemblyBindingState
{
    std::string stateId{};
    std::string parentPartId{};
    std::string parentAttachmentName{};
    glm::vec3 localPositionOffset{0.0f};
    glm::quat localRotationOffset{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
    bool visible{true};

    [[nodiscard]] bool operator==(const VoxelAssemblyBindingState& other) const = default;
};

struct VoxelAssemblyPartDefinition
{
    std::string partId{};
    std::string displayName{};
    std::string defaultModelAssetId{};
    bool visibleByDefault{true};
    std::string slotId{};
    std::string defaultStateId{};
    std::vector<VoxelAssemblyBindingState> bindingStates{};

    [[nodiscard]] bool operator==(const VoxelAssemblyPartDefinition& other) const = default;
};

struct VoxelAssemblySlotDefinition
{
    std::string slotId{};
    std::string displayName{};
    std::string fallbackPartId{};
    bool required{false};

    [[nodiscard]] bool operator==(const VoxelAssemblySlotDefinition& other) const = default;
};

class VoxelAssemblyAsset
{
public:
    std::string assetId{"untitled"};
    std::string displayName{"Untitled"};
    std::string rootPartId{};
    std::vector<VoxelAssemblyPartDefinition> parts{};
    std::vector<VoxelAssemblySlotDefinition> slots{};

    [[nodiscard]] const VoxelAssemblyPartDefinition* find_part(std::string_view partId) const
    {
        const auto it = std::ranges::find_if(parts, [&](const VoxelAssemblyPartDefinition& part)
        {
            return part.partId == partId;
        });
        return it != parts.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAssemblySlotDefinition* find_slot(std::string_view slotId) const
    {
        const auto it = std::ranges::find_if(slots, [&](const VoxelAssemblySlotDefinition& slot)
        {
            return slot.slotId == slotId;
        });
        return it != slots.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAssemblyBindingState* find_binding_state(
        std::string_view partId,
        std::string_view stateId) const
    {
        const VoxelAssemblyPartDefinition* const part = find_part(partId);
        if (part == nullptr)
        {
            return nullptr;
        }

        const auto it = std::ranges::find_if(part->bindingStates, [&](const VoxelAssemblyBindingState& state)
        {
            return state.stateId == stateId;
        });
        return it != part->bindingStates.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAssemblyBindingState* default_binding_state(std::string_view partId) const
    {
        const VoxelAssemblyPartDefinition* const part = find_part(partId);
        if (part == nullptr || part->defaultStateId.empty())
        {
            return nullptr;
        }

        return find_binding_state(partId, part->defaultStateId);
    }

    [[nodiscard]] bool operator==(const VoxelAssemblyAsset& other) const = default;
};
