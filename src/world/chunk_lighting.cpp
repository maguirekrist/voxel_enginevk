#include "chunk_lighting.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <queue>
#include <vector>

#include "terrain_gen.h"

namespace
{
    constexpr int LightDomainSize = static_cast<int>(CHUNK_SIZE) * 3;
    constexpr int LightDomainHeight = static_cast<int>(CHUNK_HEIGHT);
    constexpr int CenterOffset = static_cast<int>(CHUNK_SIZE);
    constexpr uint8_t MinimumWaterLight = 2;
    constexpr uint8_t WaterVerticalAbsorption = 1;
    constexpr uint8_t WaterLateralAbsorption = 2;
    constexpr uint8_t HorizontalAbsorption = 1;

    struct LightCell
    {
        bool solid{true};
        bool water{false};
        bool directSky{false};
        uint8_t sunlight{0};
        glm::u8vec3 localLight{0, 0, 0};
    };

    [[nodiscard]] constexpr size_t light_index(const int x, const int y, const int z) noexcept
    {
        return static_cast<size_t>(x) +
            (static_cast<size_t>(z) * static_cast<size_t>(LightDomainSize)) +
            (static_cast<size_t>(y) * static_cast<size_t>(LightDomainSize) * static_cast<size_t>(LightDomainSize));
    }

    [[nodiscard]] constexpr bool in_bounds(const int x, const int y, const int z) noexcept
    {
        return x >= 0 && x < LightDomainSize &&
            y >= 0 && y < LightDomainHeight &&
            z >= 0 && z < LightDomainSize;
    }
}

std::shared_ptr<ChunkData> ChunkLighting::solve_skylight(const ChunkNeighborhood& neighborhood)
{
    if (neighborhood.center == nullptr)
    {
        return {};
    }

    const int seaLevel = TerrainGenerator::sea_level();

    std::vector<LightCell> domain(static_cast<size_t>(LightDomainSize) * static_cast<size_t>(LightDomainSize) * static_cast<size_t>(LightDomainHeight));

    for (int x = 0; x < LightDomainSize; ++x)
    {
        for (int z = 0; z < LightDomainSize; ++z)
        {
            for (int y = 0; y < LightDomainHeight; ++y)
            {
                const auto sample = sample_block(neighborhood, x - CenterOffset, y, z - CenterOffset);
                LightCell& cell = domain[light_index(x, y, z)];
                cell.solid = !sample.has_value() || sample->block._solid;
                cell.water = sample.has_value() && sample->block._type == BlockType::WATER;
                cell.directSky = false;
                cell.sunlight = 0;
                cell.localLight = glm::u8vec3{0, 0, 0};
            }
        }
    }

    std::queue<glm::ivec3> frontier{};
    for (int x = 0; x < LightDomainSize; ++x)
    {
        for (int z = 0; z < LightDomainSize; ++z)
        {
            uint8_t sunlight = MAX_LIGHT_LEVEL;
            for (int y = LightDomainHeight - 1; y >= 0; --y)
            {
                LightCell& cell = domain[light_index(x, y, z)];
                if (cell.solid)
                {
                    sunlight = 0;
                    cell.directSky = false;
                    cell.sunlight = 0;
                    continue;
                }

                cell.sunlight = sunlight;
                cell.directSky = sunlight == MAX_LIGHT_LEVEL;
                if (cell.water && y <= seaLevel && sunlight > MinimumWaterLight)
                {
                    sunlight = static_cast<uint8_t>(std::max<int>(MinimumWaterLight, sunlight - WaterVerticalAbsorption));
                }
            }
        }
    }

    constexpr std::array<glm::ivec3, 9> PropagationOffsets{{
        { 1, 0, 0 },
        { -1, 0, 0 },
        { 0, 0, 1 },
        { 0, 0, -1 },
        { 1, 0, 1 },
        { 1, 0, -1 },
        { -1, 0, 1 },
        { -1, 0, -1 },
        { 0, -1, 0 }
    }};

    for (int x = 0; x < LightDomainSize; ++x)
    {
        for (int z = 0; z < LightDomainSize; ++z)
        {
            for (int y = 0; y < LightDomainHeight; ++y)
            {
                const glm::ivec3 current{x, y, z};
                const LightCell& cell = domain[light_index(x, y, z)];
                if (cell.solid || cell.sunlight <= 1)
                {
                    continue;
                }

                bool seedsPropagation = false;
                for (const glm::ivec3 offset : PropagationOffsets)
                {
                    const glm::ivec3 next = current + offset;
                    if (!in_bounds(next.x, next.y, next.z))
                    {
                        continue;
                    }

                    const LightCell& nextCell = domain[light_index(next.x, next.y, next.z)];
                    if (nextCell.solid)
                    {
                        continue;
                    }

                    const bool downward = offset.y < 0;
                    const uint8_t attenuation =
                        (nextCell.water && next.y <= seaLevel) ?
                        (downward ? WaterVerticalAbsorption : WaterLateralAbsorption) :
                        HorizontalAbsorption;
                    if (cell.sunlight <= attenuation)
                    {
                        continue;
                    }

                    const uint8_t propagated = static_cast<uint8_t>(cell.sunlight - attenuation);
                    if (nextCell.sunlight < propagated)
                    {
                        seedsPropagation = true;
                        break;
                    }
                }

                if (seedsPropagation)
                {
                    frontier.push(current);
                }
            }
        }
    }

    while (!frontier.empty())
    {
        const glm::ivec3 current = frontier.front();
        frontier.pop();

        const uint8_t currentLight = domain[light_index(current.x, current.y, current.z)].sunlight;
        if (currentLight <= 1)
        {
            continue;
        }

        for (const glm::ivec3 offset : PropagationOffsets)
        {
            const glm::ivec3 next = current + offset;
            if (!in_bounds(next.x, next.y, next.z))
            {
                continue;
            }

            LightCell& nextCell = domain[light_index(next.x, next.y, next.z)];
            if (nextCell.solid)
            {
                continue;
            }

            const bool downward = offset.y < 0;
            const uint8_t attenuation =
                (nextCell.water && next.y <= seaLevel) ?
                (downward ? WaterVerticalAbsorption : WaterLateralAbsorption) :
                HorizontalAbsorption;
            if (currentLight <= attenuation)
            {
                continue;
            }

            const uint8_t propagatedBase = static_cast<uint8_t>(currentLight - attenuation);
            const uint8_t propagated = (nextCell.directSky && nextCell.sunlight == MAX_LIGHT_LEVEL) ?
                nextCell.sunlight :
                (nextCell.water && propagatedBase > 0 ?
                static_cast<uint8_t>(std::max<int>(MinimumWaterLight, propagatedBase)) :
                propagatedBase);
            if (nextCell.sunlight >= propagated)
            {
                continue;
            }

            nextCell.sunlight = propagated;
            frontier.push(next);
        }
    }

    std::queue<glm::ivec3> localLightFrontier{};
    constexpr std::array<glm::ivec3, 6> LocalLightOffsets{{
        { 1, 0, 0 },
        { -1, 0, 0 },
        { 0, 1, 0 },
        { 0, -1, 0 },
        { 0, 0, 1 },
        { 0, 0, -1 }
    }};

    for (int x = 0; x < LightDomainSize; ++x)
    {
        for (int z = 0; z < LightDomainSize; ++z)
        {
            for (int y = 0; y < LightDomainHeight; ++y)
            {
                const auto sample = sample_block(neighborhood, x - CenterOffset, y, z - CenterOffset);
                if (!sample.has_value())
                {
                    continue;
                }

                const BlockEmissionDef emission = get_block_emission(sample->block._type);
                if (!emission.emits || emission.intensity == 0)
                {
                    continue;
                }

                LightCell& cell = domain[light_index(x, y, z)];
                const auto intensity = static_cast<uint16_t>(emission.intensity);
                cell.localLight = glm::u8vec3{
                    static_cast<uint8_t>((static_cast<uint16_t>(emission.color.r) * intensity) / 255),
                    static_cast<uint8_t>((static_cast<uint16_t>(emission.color.g) * intensity) / 255),
                    static_cast<uint8_t>((static_cast<uint16_t>(emission.color.b) * intensity) / 255)
                };
                localLightFrontier.push(glm::ivec3{x, y, z});
            }
        }
    }

    while (!localLightFrontier.empty())
    {
        const glm::ivec3 current = localLightFrontier.front();
        localLightFrontier.pop();

        const glm::u8vec3 currentLight = domain[light_index(current.x, current.y, current.z)].localLight;
        if (currentLight.r <= 1 && currentLight.g <= 1 && currentLight.b <= 1)
        {
            continue;
        }

        for (const glm::ivec3 offset : LocalLightOffsets)
        {
            const glm::ivec3 next = current + offset;
            if (!in_bounds(next.x, next.y, next.z))
            {
                continue;
            }

            LightCell& nextCell = domain[light_index(next.x, next.y, next.z)];
            if (nextCell.solid)
            {
                continue;
            }

            const uint8_t attenuation = (nextCell.water && next.y <= seaLevel) ? WaterLateralAbsorption : HorizontalAbsorption;
            const glm::u8vec3 propagated{
                static_cast<uint8_t>(currentLight.r > attenuation ? currentLight.r - attenuation : 0),
                static_cast<uint8_t>(currentLight.g > attenuation ? currentLight.g - attenuation : 0),
                static_cast<uint8_t>(currentLight.b > attenuation ? currentLight.b - attenuation : 0)
            };

            if (propagated.r <= nextCell.localLight.r &&
                propagated.g <= nextCell.localLight.g &&
                propagated.b <= nextCell.localLight.b)
            {
                continue;
            }

            nextCell.localLight = glm::u8vec3{
                std::max(nextCell.localLight.r, propagated.r),
                std::max(nextCell.localLight.g, propagated.g),
                std::max(nextCell.localLight.b, propagated.b)
            };
            localLightFrontier.push(next);
        }
    }

    auto litChunk = std::make_shared<ChunkData>(*neighborhood.center);
    for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            for (int y = 0; y < LightDomainHeight; ++y)
            {
                const LightCell& solved = domain[light_index(x + CenterOffset, y, z + CenterOffset)];
                litChunk->blocks[x][y][z]._sunlight = solved.sunlight;
                litChunk->blocks[x][y][z]._localLight = LocalLight{
                    .r = solved.localLight.r,
                    .g = solved.localLight.g,
                    .b = solved.localLight.b
                };
            }
        }
    }

    return litChunk;
}
