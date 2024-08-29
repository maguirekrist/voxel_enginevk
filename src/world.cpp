
#include "world.h"

//Precondition: Chunk size should be a area value
static void ds_generate(std::vector<int>& map, int count, int maxHeight) {
    float magnitude_multiplier = std::pow(2, -(0.5 * count));

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<int> dist6(-7 * magnitude_multiplier,7 * magnitude_multiplier);

    int mapSize = std::sqrt(map.size());
    int iterations = std::pow(4, count);
    int squares_per_height = std::pow(2, count);

    int distance  = ((mapSize-1) / squares_per_height);

    if(distance <= 1)
        return;

    //keep track of iteration
    //4^n represents number of iterations

    //Iterate through the chunks! Diamond Step
    int yLevel = 0;
    int xLevel = 0;
    for(int i = 0; i < iterations; i++)
    {
        int x1 = xLevel * distance;
        int y1 = (yLevel * distance);

        int x2 = (xLevel+1) * distance;
        int y2 = (yLevel * distance) + distance;

        //For each chunk, we need to compute the diamond step first
        int bottomLeft = map[(y1 * mapSize) + x1]; //bottomLeft
        int bottomRight = map[(y1 * mapSize) + x2]; //bottomRight

        int topRight = map[(y2 * mapSize) + x2]; //topRight
        int topLeft = map[(y2 * mapSize) + x1]; //topLeft

        int diamondValue = ((topLeft + topRight + bottomLeft + bottomRight) / 4) + (dist6(rng));

        int midX = ((x2 - x1) / 2) + x1;
        int midY = ((y2 - y1) / 2) + y1;

        //Diamond step
        map[(midY * mapSize) + midX] = diamondValue;

        yLevel = xLevel == squares_per_height - 1 ? yLevel + 1 : yLevel;
        xLevel = xLevel == squares_per_height - 1 ? 0 : xLevel + 1;
    }


    yLevel = 0;
    xLevel = 0;
    for(int i = 0; i < iterations; i++)
    {
        int x1 = xLevel * distance;
        int y1 = (yLevel * distance);

        int x2 = (xLevel+1) * distance;
        int y2 = (yLevel * distance) + distance;

        int midX = ((x2 - x1) / 2) + x1;
        int midY = ((y2 - y1) / 2) + y1;
        int mid_distance = (x2-x1)/2;

        int diamondValue = map[(midY * mapSize) + midX];

        int bottomLeft = map[(y1 * mapSize) + x1]; //bottomLeft
        int bottomRight = map[(y1 * mapSize) + x2]; //bottomRight

        int topRight = map[(y2 * mapSize) + x2]; //topRight
        int topLeft = map[(y2 * mapSize) + x1]; //topLeft

        // Calculate and clamp the left value
        float farLeft = x1 - mid_distance < (mapSize - 1) ? map[(midY * mapSize) + x1 + mid_distance] : map[(midY * mapSize) + x1 - mid_distance];
        int leftValue = ((diamondValue + topLeft + bottomLeft + farLeft) / 4.0f) + (dist6(rng));
        map[(midY * mapSize) + x1] = std::clamp(leftValue, 0, maxHeight);

        // Calculate and clamp the right value
        float farRight = x2 + mid_distance > (mapSize - 1) ? map[(midY * mapSize) + x2 - mid_distance] : map[(midY * mapSize) + x2 + mid_distance];
        int rightValue = ((diamondValue + bottomLeft + bottomRight + farRight) / 4) + (dist6(rng));
        map[(midY * mapSize) + x2] = std::clamp(rightValue, 0, maxHeight);

        // Calculate and clamp the bottom value
        float farBottom = y1 - mid_distance < (mapSize - 1) ? map[((y2 - mid_distance) * mapSize) + midX] : map[((y1 - mid_distance) * mapSize) + midX];
        int bottomValue = ((diamondValue + topLeft + topRight + farBottom) / 4) + (dist6(rng));
        map[(y1 * mapSize) + midX] = std::clamp(bottomValue, 0, maxHeight);

        // Calculate and clamp the top value
        float farTop = y2 + mid_distance > (mapSize - 1) ? map[((y1 + mid_distance) * mapSize) + midX] : map[((y2 + mid_distance) * mapSize) + midX];
        int topValue = ((diamondValue + topRight + bottomRight + farTop) / 4) + (dist6(rng));
        map[(y2 * mapSize) + midX] = std::clamp(topValue, 0, maxHeight);

        yLevel = xLevel == squares_per_height - 1 ? yLevel + 1 : yLevel;
        xLevel = xLevel == squares_per_height - 1 ? 0 : xLevel + 1;
    }

    //recursive call to ds_generate but on smaller chunks
    ds_generate(map, ++count, maxHeight);
}

World::World(int size)
{
    fmt::println("World Generation Starting...");
    _size = size;

    generate_height_map(size);
    update_chunk();
    chunk.generate_chunk_mesh();
}

void World::generate_height_map(int dim)
{
    _heightMap.resize(dim * dim);
    //Step 1, initialize the corners
    auto topLeft = Random::generate(1, dim);
    auto topRight = Random::generate(1, dim);
    auto bottomLeft = Random::generate(1, dim);
    auto bottomRight = Random::generate(1, dim);


    _heightMap[(0 * dim) + 0] = bottomLeft; //bottomLeft
    _heightMap[(0 * dim) + (dim-1)] = bottomRight; //bottomRight

    _heightMap[((dim-1) * dim) + (dim-1)] = topRight; //topRight
    _heightMap[((dim-1) * dim) + 0] = topLeft; //topLeft

    ds_generate(_heightMap, 0, dim);
    //heightMap[22] = 255;


    std::cout << "max height in map: " << *std::max_element(_heightMap.begin(), _heightMap.end()) << std::endl;
    std::cout << "min height in map: " << *std::min_element(_heightMap.begin(), _heightMap.end()) << std::endl;
}

void World::update_chunk()
{
    //Given the height map, we're going to update the blocks in our default chunk.
    for(int x = 0; x < _size; x++)
    {
        for(int y = 0; y < _size; y++)
        {
            int height = _heightMap[(y * _size) + x];
            for(int z = 0; z < _size; z++)
            {
                Block& block = chunk._blocks[x][z][y];
                block._position = glm::vec3(x, z, y);
                float colorVal = 0.5f;
                block._color = glm::vec3(colorVal, colorVal, colorVal);
                if(z <= height)
                {
                    block._solid = true;
                } else {
                    block._solid = false;
                }
            }
        }
    }
}
