#include "terrain_gen.h"
#include "constants.h"

constexpr float TerrainFrequency = 0.001f;

float TerrainGenerator::NormalizeHeight(std::vector<float> &map, int yScale, int xScale, int x, int y)
{
    float height = map[(y * xScale) + x];
    float normalized = (height + 1.0f) / 2.0f;
    float scaled_value = normalized * yScale;
    return scaled_value;
}

float TerrainGenerator::map_height(float noise, const std::vector<SplinePoint> &splinePoints)
{
    // Find the interval that contains the noise value
    for (size_t i = 0; i < splinePoints.size() - 1; ++i) {
        if (noise >= splinePoints[i].noiseValue && noise <= splinePoints[i + 1].noiseValue) {
            // Calculate the t value for interpolation
            float t = (noise - splinePoints[i].noiseValue) / 
                      (splinePoints[i + 1].noiseValue - splinePoints[i].noiseValue);
            
            // Interpolate linearly between the two spline points
            return lerp(splinePoints[i].heightValue, splinePoints[i + 1].heightValue, t);
        }
    }
    
    // If noise is out of the bounds of the spline points, clamp it to the nearest boundary
    if (noise < splinePoints.front().noiseValue) {
        return splinePoints.front().heightValue;
    }
    if (noise > splinePoints.back().noiseValue) {
        return splinePoints.back().heightValue;
    }

    return 0.0f; 
}

// void TerrainGenerator::GenerateChunk(int chunkX, int chunkZ, std::vector<uint8_t>& blockData) {
//     blockData.resize(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, 0); // Initialize with air (block ID 0)

//     for (int x = 0; x < CHUNK_SIZE; ++x) {
//         for (int z = 0; z < CHUNK_SIZE; ++z) {
//             int worldX = chunkX * CHUNK_SIZE + x;
//             int worldZ = chunkZ * CHUNK_SIZE + z;

//             // Get base terrain height
//             int baseHeight = GetBaseHeight(worldX, worldZ);

//             // Modify height for mountains
//             // baseHeight += static_cast<int>(GetMountainHeight(worldX, worldZ));

//             // Adjust for height limits
//             baseHeight = std::max(0, std::min(baseHeight, static_cast<int>(CHUNK_HEIGHT - 1)));

//             // // Lower terrain for rivers
//             // if (IsRiver(worldX, worldZ)) {
//             //     baseHeight -= 5; // Lower the terrain to create a riverbed
//             // }

//             for (int y = 0; y <= baseHeight; ++y) {
//                 int index = x + z * CHUNK_SIZE + y * CHUNK_SIZE * CHUNK_SIZE;

//                 // Check for caves
//                 if (IsCave(worldX, y, worldZ)) {
//                     blockData[index] = 0; // Air
//                 } else {
//                     // Assign block type based on height
//                     if (y == baseHeight) {
//                         blockData[index] = 2; // Grass block ID
//                     } else if (y > baseHeight - 5) {
//                         blockData[index] = 3; // Dirt block ID
//                     } else {
//                         blockData[index] = 1; // Stone block ID
//                     }
//                 }
//             }
//         }
//     }
// }

std::vector<float> TerrainGenerator::GenerateHeightMap(int chunkX, int chunkZ)
{
    std::vector<float> heightMap(CHUNK_SIZE * CHUNK_SIZE);
    std::vector<float> erosionMap(CHUNK_SIZE * CHUNK_SIZE);
    std::vector<float> peaksMap(CHUNK_SIZE * CHUNK_SIZE);
    std::vector<float> continentalMap(CHUNK_SIZE * CHUNK_SIZE);


    _erosion->GenUniformGrid2D(erosionMap.data(), chunkX, chunkZ, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);
    _peaks->GenUniformGrid2D(peaksMap.data(), chunkX, chunkZ, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);
    _continental->GenUniformGrid2D(continentalMap.data(), chunkX, chunkZ, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);

    for(int x = 0; x < CHUNK_SIZE * CHUNK_SIZE; x++)
    {
        float erosionHeight = map_height(erosionMap[x], _erosionSplines);
        float peaksHeight = map_height(peaksMap[x], _peakSplines);
        float continentalHeight = map_height(continentalMap[x], _continentalSplines);
        heightMap[x] = (erosionHeight + peaksHeight + continentalHeight) / 3.0f;
    }

    return heightMap;
}

// std::vector<float> TerrainGenerator::GenerateDensityMap(int chunkX, int chunkZ)
// {
//     std::vector<float> densityMap(CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT);
//     baseTerrainNoise->GenUniformGrid3D(densityMap.data(), chunkX, 0, chunkZ, CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, 0.05f, _seed);
//     return densityMap;
// }

// float TerrainGenerator::SampleNoise3D(int x, int y, int z)
// {
//     auto result = _generator->GenSingle3D(x, y, z, _seed);
//     return result;
// }
