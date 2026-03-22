#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

namespace world_lighting
{
    enum LightAffects : uint32_t
    {
        AffectWorld = 1u << 0,
        AffectProps = 1u << 1,
        AffectEntity = 1u << 2,
        AffectAll = 0xFFFFFFFFu
    };

    struct DynamicPointLight
    {
        uint64_t id{0};
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity{0.0f};
        float radius{0.0f};
        uint32_t affectMask{AffectAll};
        bool active{true};
    };

    class DynamicLightRegistry
    {
    public:
        using LightId = uint64_t;

        [[nodiscard]] LightId create(const DynamicPointLight& light);
        bool update(LightId id, const DynamicPointLight& light);
        bool remove(LightId id);
        void clear() noexcept;

        [[nodiscard]] std::optional<DynamicPointLight> find(LightId id) const;
        [[nodiscard]] const std::vector<DynamicPointLight>& snapshot() const noexcept;
        [[nodiscard]] size_t active_light_count() const noexcept;

    private:
        void rebuild_snapshot();

        LightId _nextId{1};
        std::unordered_map<LightId, DynamicPointLight> _lightsById{};
        std::vector<DynamicPointLight> _snapshot{};
    };
}
