#pragma once

#include <span>

#include <glm/vec3.hpp>

#include "dynamic_light_registry.h"
#include "game/chunk.h"

class ChunkManager;

namespace world_lighting
{
    struct SampledWorldLight
    {
        glm::vec3 bakedLocalLight{0.0f};
        float bakedSunlight{1.0f};
        glm::vec3 dynamicLight{0.0f};
    };

    [[nodiscard]] SampledWorldLight sample_baked_world_light(
        const ChunkData& chunkData,
        const glm::vec3& worldPosition) noexcept;

    [[nodiscard]] glm::vec3 sample_dynamic_point_lights(
        std::span<const DynamicPointLight> lights,
        const glm::vec3& worldPosition,
        uint32_t affectMask) noexcept;

    class WorldLightSampler
    {
    public:
        WorldLightSampler(const ChunkManager& chunkManager, const DynamicLightRegistry& dynamicLights) noexcept;

        [[nodiscard]] SampledWorldLight sample(const glm::vec3& worldPosition, uint32_t affectMask) const noexcept;

    private:
        const ChunkManager* _chunkManager{nullptr};
        const DynamicLightRegistry* _dynamicLights{nullptr};
    };
}
