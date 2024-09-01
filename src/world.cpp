
#include "world.h"

template <typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

template <Arithmetic T>
void median_filter(std::vector<T>& map, int mapSize, int kernel_size)
{
    if (kernel_size % 2 == 0) {
        throw std::invalid_argument("Kernel size must be odd.");
    }

    int edge = kernel_size / 2;

    auto get_ordered_list = [&](int x, int y) -> std::vector<T> {
        std::vector<T> pixels;

        for (int i = -edge; i <= edge; i++) {
            for (int j = -edge; j <= edge; j++) {
                int nx = x + j;
                int ny = y + i;

                // Clamp coordinates to be within bounds
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx >= mapSize) nx = mapSize - 1;
                if (ny >= mapSize) ny = mapSize - 1;

                pixels.push_back(map[(mapSize * ny) + nx]);
            }
        }

        std::ranges::sort(pixels);

        return pixels;
    };

    std::vector<T> original_map = map; // Copy the original map to use for non-edge areas

    // Filter the inner part of the map
    for (int i = edge; i < (mapSize - edge); i++) {
        for (int j = edge; j < (mapSize - edge); j++) {
            // Construct an ordered list of the pixels in the kernel window
            auto olist = get_ordered_list(j, i);
            map[(mapSize * i) + j] = olist[olist.size() / 2]; // Set the pixel to the median value
        }
    }

    // Handle the borders (optional based on your application)
    for (int i = 0; i < mapSize; i++) {
        for (int j = 0; j < mapSize; j++) {
            if (i < edge || i >= (mapSize - edge) || j < edge || j >= (mapSize - edge)) {
                // Optionally, handle borders differently if required
                // For now, copying the original map values (optional)
                map[(mapSize * i) + j] = original_map[(mapSize * i) + j];
            }
        }
    }
}

World::World()
{
    fmt::println("World Generation Starting...");
    _seed = Random::generate(1, 1337);
    auto perlin = FastNoise::New<FastNoise::Perlin>();
    _generator.base = perlin;


    //Test setup, generate a bunch of centered around the origin (0, 0, 0)
    int world_size_chunks = 32; //4 chunks in each direction, n/s/e/w

    //std::vector<glm::ivec2> direction_offsets = { { 0, CHUNK_SIZE }, { 0, -CHUNK_SIZE }, { -CHUNK_SIZE, 0 }, { CHUNK_SIZE, 0 } };

    for (int x = 0; x < world_size_chunks; x++)
    {
        for (int y = 0; y < world_size_chunks; y++)
        {
            auto xstart = x * CHUNK_SIZE;
            auto ystart = y * CHUNK_SIZE;
            generate_chunk(xstart, ystart);
        }
    }

    //generate_chunk(0, 0);

    fmt::println("World Generation Completed...");
}

void World::generate_chunk(int xStart, int yStart)
{
    std::vector<float> heightMap(CHUNK_SIZE * CHUNK_SIZE);
    _generator.base->GenUniformGrid2D(heightMap.data(), xStart, yStart, CHUNK_SIZE, CHUNK_SIZE, 0.2f, _seed);

    auto chunk = std::make_unique<Chunk>(glm::ivec2(xStart, yStart));
    update_chunk(*chunk, heightMap);
    _chunks.push_back(std::move(chunk));
}

float get_normalized_height(std::vector<float>& map, int yScale, int xScale, int x, int y)
{
    float height = map[(y * xScale) + x];
    float normalized = (height + 1.0f) / 2.0f;
    float scaled_value = normalized * yScale;
    return scaled_value;
}

void World::update_chunk(Chunk& chunk, std::vector<float>& heightMap)
{
    //Given the height map, we're going to update the blocks in our default chunk.
    for(int x = 0; x < CHUNK_SIZE; x++)
    {
        for(int z = 0; z < CHUNK_SIZE; z++)
        {
            auto height = get_normalized_height(heightMap, CHUNK_HEIGHT, CHUNK_SIZE, x, z);
            for (int y = 0; y < CHUNK_HEIGHT; y++)
            {
                Block& block = chunk._blocks[x][y][z];
                block._position = glm::vec3(x, y, z);
                float colorVal = 0.5f;
                block._color = glm::vec3(colorVal, colorVal, colorVal);
                if (y <= height)
                {
                    block._solid = true;
                }
                else {
                    block._solid = false;
                }
            }
        }
    }

}
