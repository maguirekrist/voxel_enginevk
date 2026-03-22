#include "world_light_sampler.h"

#include <algorithm>
#include <cmath>

#include "constants.h"
#include "game/world.h"
#include "world/chunk_manager.h"

namespace
{
    [[nodiscard]] glm::ivec3 floor_world_position(const glm::vec3& worldPosition) noexcept
    {
        return glm::ivec3(
            static_cast<int>(std::floor(worldPosition.x)),
            static_cast<int>(std::floor(worldPosition.y)),
            static_cast<int>(std::floor(worldPosition.z)));
    }
}

namespace world_lighting
{
    SampledWorldLight sample_baked_world_light(const ChunkData& chunkData, const glm::vec3& worldPosition) noexcept
    {
        SampledWorldLight sampled{};
        const glm::ivec3 blockWorldPosition = floor_world_position(worldPosition);
        if (!chunkData.contains_world_position(blockWorldPosition))
        {
            return sampled;
        }

        const glm::ivec3 local = chunkData.to_local_position(blockWorldPosition);
        const Block& block = chunkData.blocks[local.x][local.y][local.z];
        sampled.bakedSunlight = static_cast<float>(block._sunlight) / static_cast<float>(MAX_LIGHT_LEVEL);
        sampled.bakedLocalLight = glm::vec3(
            static_cast<float>(block._localLight.r),
            static_cast<float>(block._localLight.g),
            static_cast<float>(block._localLight.b)) / static_cast<float>(MAX_LIGHT_LEVEL);
        return sampled;
    }

    glm::vec3 sample_dynamic_point_lights(
        const std::span<const DynamicPointLight> lights,
        const glm::vec3& worldPosition,
        const uint32_t affectMask) noexcept
    {
        glm::vec3 accumulated{0.0f};

        for (const DynamicPointLight& light : lights)
        {
            if (!light.active || (light.affectMask & affectMask) == 0 || light.radius <= 0.0f || light.intensity <= 0.0f)
            {
                continue;
            }

            const glm::vec3 toLight = light.position - worldPosition;
            const float distanceSquared = glm::dot(toLight, toLight);
            const float radiusSquared = light.radius * light.radius;
            if (distanceSquared >= radiusSquared)
            {
                continue;
            }

            const float distance = std::sqrt(distanceSquared);
            const float attenuation = 1.0f - std::clamp(distance / light.radius, 0.0f, 1.0f);
            const float falloff = attenuation * attenuation * light.intensity;
            accumulated += light.color * falloff;
        }

        return accumulated;
    }

    WorldLightSampler::WorldLightSampler(
        const ChunkManager& chunkManager,
        const DynamicLightRegistry& dynamicLights) noexcept :
        _chunkManager(&chunkManager),
        _dynamicLights(&dynamicLights)
    {
    }

    SampledWorldLight WorldLightSampler::sample(const glm::vec3& worldPosition, const uint32_t affectMask) const noexcept
    {
        SampledWorldLight sampled{};
        if (_dynamicLights != nullptr)
        {
            sampled.dynamicLight = sample_dynamic_point_lights(_dynamicLights->snapshot(), worldPosition, affectMask);
        }

        if (_chunkManager == nullptr)
        {
            return sampled;
        }

        Chunk* const chunk = _chunkManager->get_chunk(World::get_chunk_coordinates(worldPosition));
        if (chunk == nullptr || chunk->_data == nullptr)
        {
            return sampled;
        }

        sampled = sample_baked_world_light(*chunk->_data, worldPosition);
        if (_dynamicLights != nullptr)
        {
            sampled.dynamicLight = sample_dynamic_point_lights(_dynamicLights->snapshot(), worldPosition, affectMask);
        }
        return sampled;
    }
}
