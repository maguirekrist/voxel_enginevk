#pragma once 

constexpr unsigned int CHUNK_SIZE = 16;
constexpr unsigned int CHUNK_HEIGHT = 256;
constexpr unsigned int MAX_LIGHT_LEVEL = 15;

constexpr unsigned int SEA_LEVEL = 62;

//number is in chunks away from player position.

namespace GameConfig
{
    constexpr int DEFAULT_VIEW_DISTANCE = 16;
    constexpr int MAXIMUM_CHUNKS = (2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1);
    constexpr float DEFAULT_MOVE_SPEED = 40.0f;
    constexpr float DEFAULT_ROTATION_SPEED = 180.0f;
}

constexpr bool USE_VALIDATION_LAYERS = true;
constexpr bool USE_IMGUI = true;

constexpr unsigned int FRAME_OVERLAP = 1;
