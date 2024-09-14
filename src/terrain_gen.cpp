#include "terrain_gen.h"
#include "FastNoise/Generators/Perlin.h"
#include "FastNoise/Generators/Simplex.h"
#include "FastNoise/SmartNode.h"
#include "random.h"
#include "chunk.h"

int TerrainGenerator::_seed = Random::generate(0, 1337);
FastNoise::GeneratorSource TerrainGenerator::_generator;

FastNoise::SmartNode<FastNoise::Perlin> TerrainGenerator::_terrainNoise = FastNoise::New<FastNoise::Perlin>();  
FastNoise::SmartNode<FastNoise::Simplex> TerrainGenerator::_caveNoise = FastNoise::New<FastNoise::Simplex>();  
FastNoise::SmartNode<FastNoise::Perlin> TerrainGenerator::_riverNoise = FastNoise::New<FastNoise::Perlin>();  


std::vector<int> TerrainGenerator::generate_height_map(int xStart, int zStart)
{

    std::vector<int> heightMap(CHUNK_SIZE * CHUNK_SIZE);
    // for (int x = 0; x < CHUNK_SIZE; ++x) {
    //     for (int z = 0; z < CHUNK_SIZE; ++z) {
    //         // Generate height using 2D Perlin noise
    //         float heightValue = _terrainNoise->GenSingle2D((float)(xStart * CHUNK_SIZE + x), (float)(zStart * CHUNK_SIZE + z), _seed);
    //         // Map noise value to terrain height (between 0 and MAX_HEIGHT)
    //         int height = static_cast<int>((heightValue + 1.0f) * 0.5f * CHUNK_HEIGHT);  // Scale to [0, MAX_HEIGHT]
    //         heightMap[(x * CHUNK_SIZE) + z] = height;
    //     }
    // }
    std::vector<float> noiseOut(CHUNK_SIZE * CHUNK_SIZE);
    TerrainGenerator::_terrainNoise->GenUniformGrid2D(noiseOut.data(), xStart, zStart, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);

    // for(int x )

    add_rivers(heightMap, xStart, zStart);

    return heightMap;
}

void TerrainGenerator::add_rivers(std::vector<int>& heightMap, int chunkX, int chunkZ)
{
    for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                float riverValue = _riverNoise->GenSingle2D((float)(chunkX * CHUNK_SIZE + x), (float)(chunkZ * CHUNK_SIZE + z), _seed);
                if (riverValue < RIVER_THRESHOLD) {
                    // Lower the terrain to create a river
                    heightMap[(x * CHUNK_SIZE) + z] = std::min(heightMap[(x * CHUNK_SIZE) + z], RIVER_HEIGHT);
                }
            }
        }
}

float TerrainGenerator::get_normalized_height(std::vector<float>& map, int yScale, int xScale, int x, int y)
{
    float height = map[(y * xScale) + x];
    float normalized = (height + 1.0f) / 2.0f;
    float scaled_value = normalized * yScale;
    return scaled_value;
}
