#include "chunk_lighting.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <tracy/Tracy.hpp>

#include "terrain_gen.h"

namespace
{
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

    struct LightCoord
    {
        uint16_t x{0};
        uint16_t y{0};
        uint16_t z{0};
    };

    struct ColumnSkylightInfo
    {
        int16_t highestSolidY{-1};
        bool hasLitWater{false};
    };

    struct LightingScratch
    {
        std::vector<LightCell> domain{};
        std::vector<LightCoord> skylightFrontier{};
        std::vector<LightCoord> localLightFrontier{};
        std::vector<ColumnSkylightInfo> columnSkylightInfo{};
        std::vector<uint8_t> queuedSeedCells{};
    };

    thread_local LightingScratch g_lightingScratch{};

    [[nodiscard]] constexpr size_t light_index(const int domainSize, const int x, const int y, const int z) noexcept
    {
        return static_cast<size_t>(x) +
            (static_cast<size_t>(z) * static_cast<size_t>(domainSize)) +
            (static_cast<size_t>(y) * static_cast<size_t>(domainSize) * static_cast<size_t>(domainSize));
    }

    [[nodiscard]] constexpr bool in_bounds(const int domainSize, const int domainHeight, const int x, const int y, const int z) noexcept
    {
        return x >= 0 && x < domainSize &&
            y >= 0 && y < domainHeight &&
            z >= 0 && z < domainSize;
    }

    [[nodiscard]] constexpr uint8_t sunlight_attenuation(const bool water, const int y, const int seaLevel, const bool downward) noexcept
    {
        if (water && y <= seaLevel)
        {
            return downward ? WaterVerticalAbsorption : WaterLateralAbsorption;
        }

        return HorizontalAbsorption;
    }
}

std::shared_ptr<ChunkData> ChunkLighting::solve_skylight(const ChunkNeighborhood& neighborhood)
{
    ZoneScopedN("ChunkLighting::Solve");
    if (neighborhood.center == nullptr || !neighborhood.center->has_block_storage())
    {
        return {};
    }

    const int seaLevel = TerrainGenerator::sea_level();
    const int chunkVoxelWidth = neighborhood.center->voxelWidth;
    const int chunkVoxelHeight = neighborhood.center->voxelHeight;
    const int lightHalo = std::min<int>(chunkVoxelWidth, MAX_LIGHT_LEVEL);
    const int lightDomainSize = chunkVoxelWidth + (lightHalo * 2);
    const int lightDomainHeight = chunkVoxelHeight;
    const int centerOffset = lightHalo;
    const size_t lightDomainPlaneSize = static_cast<size_t>(lightDomainSize) * static_cast<size_t>(lightDomainSize);
    const size_t lightDomainCellCount = lightDomainPlaneSize * static_cast<size_t>(lightDomainHeight);

    const ChunkData* const centerChunk = neighborhood.center.get();
    const ChunkData* const negXChunk = neighborhood.get_by_offset(-1, 0);
    const ChunkData* const posXChunk = neighborhood.get_by_offset(1, 0);
    const ChunkData* const negZChunk = neighborhood.get_by_offset(0, -1);
    const ChunkData* const posZChunk = neighborhood.get_by_offset(0, 1);
    const ChunkData* const negXNegZChunk = neighborhood.get_by_offset(-1, -1);
    const ChunkData* const posXNegZChunk = neighborhood.get_by_offset(1, -1);
    const ChunkData* const negXPosZChunk = neighborhood.get_by_offset(-1, 1);
    const ChunkData* const posXPosZChunk = neighborhood.get_by_offset(1, 1);
    const bool neighborhoodHasEmitters =
        (centerChunk != nullptr && centerChunk->has_emissive_blocks()) ||
        (negXChunk != nullptr && negXChunk->has_emissive_blocks()) ||
        (posXChunk != nullptr && posXChunk->has_emissive_blocks()) ||
        (negZChunk != nullptr && negZChunk->has_emissive_blocks()) ||
        (posZChunk != nullptr && posZChunk->has_emissive_blocks()) ||
        (negXNegZChunk != nullptr && negXNegZChunk->has_emissive_blocks()) ||
        (posXNegZChunk != nullptr && posXNegZChunk->has_emissive_blocks()) ||
        (negXPosZChunk != nullptr && negXPosZChunk->has_emissive_blocks()) ||
        (posXPosZChunk != nullptr && posXPosZChunk->has_emissive_blocks());

    auto& domain = g_lightingScratch.domain;
    domain.assign(lightDomainCellCount, LightCell{});
    auto& localLightFrontier = g_lightingScratch.localLightFrontier;
    localLightFrontier.clear();
    if (neighborhoodHasEmitters)
    {
        localLightFrontier.reserve(64);
    }
    auto& skylightFrontier = g_lightingScratch.skylightFrontier;
    skylightFrontier.clear();
    skylightFrontier.reserve(static_cast<size_t>(lightDomainSize) * static_cast<size_t>(lightDomainSize));
    auto& columnSkylightInfo = g_lightingScratch.columnSkylightInfo;
    columnSkylightInfo.assign(static_cast<size_t>(lightDomainSize) * static_cast<size_t>(lightDomainSize), ColumnSkylightInfo{});

    {
        ZoneScopedN("ChunkLighting::BuildDomain");
        const auto fill_region = [&](const ChunkData* const sourceChunk,
                                     const int dstXStart,
                                     const int dstZStart,
                                     const int regionWidth,
                                     const int regionDepth,
                                     const int srcXStart,
                                     const int srcZStart)
        {
            if (sourceChunk == nullptr || !sourceChunk->has_block_storage())
            {
                return;
            }

            for (int localX = 0; localX < regionWidth; ++localX)
            {
                const int dstX = dstXStart + localX;
                const int srcX = srcXStart + localX;
                for (int localZ = 0; localZ < regionDepth; ++localZ)
                {
                    const int dstZ = dstZStart + localZ;
                    const int srcZ = srcZStart + localZ;
                    size_t cellIndex =
                        static_cast<size_t>(dstX) +
                        (static_cast<size_t>(dstZ) * static_cast<size_t>(lightDomainSize));
                    for (int y = 0; y < lightDomainHeight; ++y)
                    {
                        const size_t currentCellIndex = cellIndex;
                        cellIndex += lightDomainPlaneSize;

                        LightCell& cell = domain[currentCellIndex];
                        const Block& block = sourceChunk->blocks[srcX][y][srcZ];
                        cell.solid = block._solid;
                        cell.water = block._type == BlockType::WATER;
                        cell.directSky = false;
                        cell.sunlight = 0;
                        cell.localLight = glm::u8vec3{0, 0, 0};

                        if (!neighborhoodHasEmitters)
                        {
                            continue;
                        }

                        const BlockEmissionDef emission = get_block_emission(block._type);
                        if (!emission.emits || emission.intensity == 0)
                        {
                            continue;
                        }

                        const auto intensity = static_cast<uint16_t>(emission.intensity);
                        cell.localLight = glm::u8vec3{
                            static_cast<uint8_t>((static_cast<uint16_t>(emission.color.r) * intensity) / 255),
                            static_cast<uint8_t>((static_cast<uint16_t>(emission.color.g) * intensity) / 255),
                            static_cast<uint8_t>((static_cast<uint16_t>(emission.color.b) * intensity) / 255)
                        };
                        localLightFrontier.push_back(LightCoord{
                            .x = static_cast<uint16_t>(dstX),
                            .y = static_cast<uint16_t>(y),
                            .z = static_cast<uint16_t>(dstZ)
                        });
                    }
                }
            }
        };

        fill_region(negXNegZChunk, 0, 0, lightHalo, lightHalo, chunkVoxelWidth - lightHalo, chunkVoxelWidth - lightHalo);
        fill_region(negZChunk, centerOffset, 0, chunkVoxelWidth, lightHalo, 0, chunkVoxelWidth - lightHalo);
        fill_region(posXNegZChunk, centerOffset + chunkVoxelWidth, 0, lightHalo, lightHalo, 0, chunkVoxelWidth - lightHalo);

        fill_region(negXChunk, 0, centerOffset, lightHalo, chunkVoxelWidth, chunkVoxelWidth - lightHalo, 0);
        fill_region(centerChunk, centerOffset, centerOffset, chunkVoxelWidth, chunkVoxelWidth, 0, 0);
        fill_region(posXChunk, centerOffset + chunkVoxelWidth, centerOffset, lightHalo, chunkVoxelWidth, 0, 0);

        fill_region(negXPosZChunk, 0, centerOffset + chunkVoxelWidth, lightHalo, lightHalo, chunkVoxelWidth - lightHalo, 0);
        fill_region(posZChunk, centerOffset, centerOffset + chunkVoxelWidth, chunkVoxelWidth, lightHalo, 0, 0);
        fill_region(posXPosZChunk, centerOffset + chunkVoxelWidth, centerOffset + chunkVoxelWidth, lightHalo, lightHalo, 0, 0);
    }

    {
        ZoneScopedN("ChunkLighting::SeedSkylight");
        for (int x = 0; x < lightDomainSize; ++x)
        {
            for (int z = 0; z < lightDomainSize; ++z)
            {
                ColumnSkylightInfo& columnInfo =
                    columnSkylightInfo[static_cast<size_t>(x) + (static_cast<size_t>(z) * static_cast<size_t>(lightDomainSize))];
                uint8_t sunlight = MAX_LIGHT_LEVEL;
                size_t cellIndex =
                    static_cast<size_t>(x) +
                    (static_cast<size_t>(z) * static_cast<size_t>(lightDomainSize)) +
                    (static_cast<size_t>(lightDomainHeight - 1) * lightDomainPlaneSize);
                for (int y = lightDomainHeight - 1; y >= 0; --y)
                {
                    const size_t currentCellIndex = cellIndex;
                    if (y > 0)
                    {
                        cellIndex -= lightDomainPlaneSize;
                    }

                    LightCell& cell = domain[currentCellIndex];
                    if (cell.solid)
                    {
                        if (columnInfo.highestSolidY < 0)
                        {
                            columnInfo.highestSolidY = static_cast<int16_t>(y);
                        }
                        sunlight = 0;
                        cell.directSky = false;
                        cell.sunlight = 0;
                        continue;
                    }

                    cell.sunlight = sunlight;
                    cell.directSky = sunlight == MAX_LIGHT_LEVEL;
                    if (sunlight > 0 && cell.water && y <= seaLevel)
                    {
                        columnInfo.hasLitWater = true;
                    }
                    if (cell.water && y <= seaLevel && sunlight > MinimumWaterLight)
                    {
                        sunlight = static_cast<uint8_t>(std::max<int>(MinimumWaterLight, sunlight - WaterVerticalAbsorption));
                    }
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

    {
        ZoneScopedN("ChunkLighting::SeedSkylightPropagation");
        constexpr std::array<glm::ivec2, 4> SeedOffsets{{
            { 1, 0 },
            { 0, 1 },
            { 1, 1 },
            { -1, 1 }
        }};
        auto& queuedSeedCells = g_lightingScratch.queuedSeedCells;
        queuedSeedCells.assign(domain.size(), 0);
        int64_t fullScanPairCount = 0;
        int64_t optimizedPairCount = 0;
        int64_t skippedIdenticalDryPairCount = 0;

        const auto maybe_seed_pair = [&](const int fromX, const int y, const int fromZ, const int toX, const int toZ)
        {
            LightCell& fromCell = domain[light_index(lightDomainSize, fromX, y, fromZ)];
            LightCell& toCell = domain[light_index(lightDomainSize, toX, y, toZ)];
            if (fromCell.solid || toCell.solid || fromCell.sunlight <= 1)
            {
                return;
            }

            const uint8_t attenuation = sunlight_attenuation(toCell.water, y, seaLevel, false);
            if (fromCell.sunlight <= attenuation)
            {
                return;
            }

            const uint8_t propagated = static_cast<uint8_t>(fromCell.sunlight - attenuation);
            if (toCell.sunlight >= propagated)
            {
                return;
            }

            const size_t fromIndex = light_index(lightDomainSize, fromX, y, fromZ);
            if (queuedSeedCells[fromIndex] != 0)
            {
                return;
            }

            queuedSeedCells[fromIndex] = 1;
            skylightFrontier.push_back(LightCoord{
                .x = static_cast<uint16_t>(fromX),
                .y = static_cast<uint16_t>(y),
                .z = static_cast<uint16_t>(fromZ)
            });
        };

        const auto column_info_at = [&](const int x, const int z) -> const ColumnSkylightInfo&
        {
            return columnSkylightInfo[static_cast<size_t>(x) + (static_cast<size_t>(z) * static_cast<size_t>(lightDomainSize))];
        };

        const auto seed_fully_scanned_pair = [&](const int x, const int z, const int nextX, const int nextZ)
        {
            ++fullScanPairCount;
            for (int y = 0; y < lightDomainHeight; ++y)
            {
                maybe_seed_pair(x, y, z, nextX, nextZ);
                maybe_seed_pair(nextX, y, nextZ, x, z);
            }
        };

        for (int z = 0; z < lightDomainSize; ++z)
        {
            for (int x = 0; x < lightDomainSize; ++x)
            {
                for (const glm::ivec2 offset : SeedOffsets)
                {
                    const int nextX = x + offset.x;
                    const int nextZ = z + offset.y;
                    if (!in_bounds(lightDomainSize, lightDomainHeight, nextX, 0, nextZ))
                    {
                        continue;
                    }

                    const ColumnSkylightInfo& currentInfo = column_info_at(x, z);
                    const ColumnSkylightInfo& nextInfo = column_info_at(nextX, nextZ);
                    if (currentInfo.hasLitWater || nextInfo.hasLitWater)
                    {
                        seed_fully_scanned_pair(x, z, nextX, nextZ);
                        continue;
                    }

                    if (currentInfo.highestSolidY == nextInfo.highestSolidY)
                    {
                        ++skippedIdenticalDryPairCount;
                        continue;
                    }

                    ++optimizedPairCount;
                    const bool currentIsBrighter = currentInfo.highestSolidY < nextInfo.highestSolidY;
                    const int fromX = currentIsBrighter ? x : nextX;
                    const int fromZ = currentIsBrighter ? z : nextZ;
                    const int toX = currentIsBrighter ? nextX : x;
                    const int toZ = currentIsBrighter ? nextZ : z;
                    const int brighterSolidY = currentIsBrighter ? currentInfo.highestSolidY : nextInfo.highestSolidY;
                    const int darkerSolidY = currentIsBrighter ? nextInfo.highestSolidY : currentInfo.highestSolidY;
                    const int startY = std::max<int>(brighterSolidY + 1, 0);
                    const int endY = darkerSolidY - 1;
                    if (endY < startY)
                    {
                        continue;
                    }

                    for (int y = startY; y <= endY; ++y)
                    {
                        maybe_seed_pair(fromX, y, fromZ, toX, toZ);
                    }
                }
            }
        }
        TracyPlot("ChunkLighting Skylight Full Scan Pairs", fullScanPairCount);
        TracyPlot("ChunkLighting Skylight Optimized Pairs", optimizedPairCount);
        TracyPlot("ChunkLighting Skylight Skipped Dry Pairs", skippedIdenticalDryPairCount);
    }
    TracyPlot("ChunkLighting Skylight Frontier Seeds", static_cast<int64_t>(skylightFrontier.size()));

    {
        ZoneScopedN("ChunkLighting::PropagateSkylight");
        size_t frontierHead = 0;
        while (frontierHead < skylightFrontier.size())
        {
            const LightCoord current = skylightFrontier[frontierHead++];
            const uint8_t currentLight = domain[light_index(lightDomainSize, current.x, current.y, current.z)].sunlight;
            if (currentLight <= 1)
            {
                continue;
            }

            for (const glm::ivec3 offset : PropagationOffsets)
            {
                const int nextX = static_cast<int>(current.x) + offset.x;
                const int nextY = static_cast<int>(current.y) + offset.y;
                const int nextZ = static_cast<int>(current.z) + offset.z;
                if (!in_bounds(lightDomainSize, lightDomainHeight, nextX, nextY, nextZ))
                {
                    continue;
                }

                LightCell& nextCell = domain[light_index(lightDomainSize, nextX, nextY, nextZ)];
                if (nextCell.solid)
                {
                    continue;
                }

                const uint8_t attenuation = sunlight_attenuation(nextCell.water, nextY, seaLevel, offset.y < 0);
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
                skylightFrontier.push_back(LightCoord{
                    .x = static_cast<uint16_t>(nextX),
                    .y = static_cast<uint16_t>(nextY),
                    .z = static_cast<uint16_t>(nextZ)
                });
            }
        }
    }

    {
        ZoneScopedN("ChunkLighting::SeedLocalLight");
        TracyPlot("ChunkLighting Local Emitters", static_cast<int64_t>(localLightFrontier.size()));
    }
    TracyPlot("ChunkLighting Local Frontier Seeds", static_cast<int64_t>(localLightFrontier.size()));

    constexpr std::array<glm::ivec3, 6> LocalLightOffsets{{
        { 1, 0, 0 },
        { -1, 0, 0 },
        { 0, 1, 0 },
        { 0, -1, 0 },
        { 0, 0, 1 },
        { 0, 0, -1 }
    }};

    if (neighborhoodHasEmitters && !localLightFrontier.empty())
    {
        ZoneScopedN("ChunkLighting::PropagateLocalLight");
        size_t frontierHead = 0;
        while (frontierHead < localLightFrontier.size())
        {
            const LightCoord current = localLightFrontier[frontierHead++];
            const glm::u8vec3 currentLight = domain[light_index(lightDomainSize, current.x, current.y, current.z)].localLight;
            if (currentLight.r <= 1 && currentLight.g <= 1 && currentLight.b <= 1)
            {
                continue;
            }

            for (const glm::ivec3 offset : LocalLightOffsets)
            {
                const int nextX = static_cast<int>(current.x) + offset.x;
                const int nextY = static_cast<int>(current.y) + offset.y;
                const int nextZ = static_cast<int>(current.z) + offset.z;
                if (!in_bounds(lightDomainSize, lightDomainHeight, nextX, nextY, nextZ))
                {
                    continue;
                }

                LightCell& nextCell = domain[light_index(lightDomainSize, nextX, nextY, nextZ)];
                if (nextCell.solid)
                {
                    continue;
                }

                const uint8_t attenuation = (nextCell.water && nextY <= seaLevel) ? WaterLateralAbsorption : HorizontalAbsorption;
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
                localLightFrontier.push_back(LightCoord{
                    .x = static_cast<uint16_t>(nextX),
                    .y = static_cast<uint16_t>(nextY),
                    .z = static_cast<uint16_t>(nextZ)
                });
            }
        }
    }

    {
        ZoneScopedN("ChunkLighting::WriteBackCenterChunk");
        auto litChunk = std::make_shared<ChunkData>(*neighborhood.center);
        for (int x = 0; x < chunkVoxelWidth; ++x)
        {
            for (int z = 0; z < chunkVoxelWidth; ++z)
            {
                size_t cellIndex =
                    static_cast<size_t>(x + centerOffset) +
                    (static_cast<size_t>(z + centerOffset) * static_cast<size_t>(lightDomainSize));
                for (int y = 0; y < lightDomainHeight; ++y)
                {
                    const LightCell& solved = domain[cellIndex];
                    litChunk->blocks[x][y][z]._sunlight = solved.sunlight;
                    litChunk->blocks[x][y][z]._localLight = LocalLight{
                        .r = solved.localLight.r,
                        .g = solved.localLight.g,
                        .b = solved.localLight.b
                    };
                    cellIndex += lightDomainPlaneSize;
                }
            }
        }

        return litChunk;
    }
}
