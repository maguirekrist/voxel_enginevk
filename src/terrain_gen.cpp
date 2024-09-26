#include "terrain_gen.h"
#include "constants.h"

constexpr float TerrainFrequency = 0.001f;

int TerrainGenerator::GetBaseHeight(int x, int z) {
    float noiseValue = baseTerrainNoise->GenSingle2D((float)x * TerrainFrequency, (float)z * TerrainFrequency, _seed);
    int height = static_cast<int>((noiseValue + 1.0f) * 0.5f * (CHUNK_HEIGHT / 2)) + (CHUNK_HEIGHT / 4);
    return height;
}

float TerrainGenerator::GetMountainHeight(int x, int z) {
    float noiseValue = mountainNoise->GenSingle2D((float)x, (float)z, _seed);
    float height = (noiseValue > 0.6f) ? (noiseValue - 0.6f) * CHUNK_HEIGHT / 2 : 0.0f;
    return height;
}

bool TerrainGenerator::IsRiver(int x, int z) {
    float noiseValue = riverNoise->GenSingle2D((float)x, (float)z, _seed);
    return fabs(noiseValue) < 0.05f;
}

bool TerrainGenerator::IsCave(int x, int y, int z) {
    return false;
    // float noiseValue = caveNoise->GenSingle3D((float)x, (float)y, (float)z, _seed);
    // return noiseValue > 0.7f;
}

float TerrainGenerator::NormalizeHeight(std::vector<float> &map, int yScale, int xScale, int x, int y)
{
    float height = map[(y * xScale) + x];
    float normalized = (height + 1.0f) / 2.0f;
    float scaled_value = normalized * yScale;
    return scaled_value;
}

void TerrainGenerator::GenerateChunk(int chunkX, int chunkZ, std::vector<uint8_t>& blockData) {
    blockData.resize(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, 0); // Initialize with air (block ID 0)

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            int worldX = chunkX * CHUNK_SIZE + x;
            int worldZ = chunkZ * CHUNK_SIZE + z;

            // Get base terrain height
            int baseHeight = GetBaseHeight(worldX, worldZ);

            // Modify height for mountains
            // baseHeight += static_cast<int>(GetMountainHeight(worldX, worldZ));

            // Adjust for height limits
            baseHeight = std::max(0, std::min(baseHeight, static_cast<int>(CHUNK_HEIGHT - 1)));

            // // Lower terrain for rivers
            // if (IsRiver(worldX, worldZ)) {
            //     baseHeight -= 5; // Lower the terrain to create a riverbed
            // }

            for (int y = 0; y <= baseHeight; ++y) {
                int index = x + z * CHUNK_SIZE + y * CHUNK_SIZE * CHUNK_SIZE;

                // Check for caves
                if (IsCave(worldX, y, worldZ)) {
                    blockData[index] = 0; // Air
                } else {
                    // Assign block type based on height
                    if (y == baseHeight) {
                        blockData[index] = 2; // Grass block ID
                    } else if (y > baseHeight - 5) {
                        blockData[index] = 3; // Dirt block ID
                    } else {
                        blockData[index] = 1; // Stone block ID
                    }
                }
            }
        }
    }
}

std::vector<float> TerrainGenerator::GenerateHeightMap(int chunkX, int chunkZ)
{
    std::vector<float> heightMap(CHUNK_SIZE * CHUNK_SIZE);
    baseTerrainNoise->GenUniformGrid2D(heightMap.data(), chunkX, chunkZ, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);
    return heightMap;
}

std::vector<float> TerrainGenerator::GenerateDensityMap(int chunkX, int chunkZ)
{
    std::vector<float> densityMap(CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT);
    baseTerrainNoise->GenUniformGrid3D(densityMap.data(), chunkX, 0, chunkZ, CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, 0.05f, _seed);
    return densityMap;
}

float TerrainGenerator::SampleNoise3D(int x, int y, int z)
{
    auto result = _generator->GenSingle3D(x, y, z, _seed);
    return result;
}
