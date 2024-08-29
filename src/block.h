
#pragma once
#include <vk_types.h>

enum FaceDirection {
    FRONT_FACE = 0,
    BACK_FACE = 1,
    LEFT_FACE = 2,
    RIGHT_FACE = 3,
    TOP_FACE = 4,
    BOTTOM_FACE = 5
};

constexpr FaceDirection faceDirections[6] = { FRONT_FACE, BACK_FACE, LEFT_FACE, RIGHT_FACE, TOP_FACE, BOTTOM_FACE };

constexpr int faceOffsetX[] = { 0,  0, -1,  1,  0,  0 };
constexpr int faceOffsetY[] = { 0,  0,  0,  0,  1, -1 };
constexpr int faceOffsetZ[] = { 1, -1,  0,  0,  0,  0 };

constexpr glm::vec3 faceVertices[6][4] = {
    // FRONT_FACE
    { {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1} },
    // BACK_FACE
    { {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0} },
    // LEFT_FACE
    { {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0} },
    // RIGHT_FACE
    { {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1} },
    // TOP_FACE
    { {0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0} },
    // BOTTOM_FACE
    { {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1} }
};

struct Block {
    bool _solid = false;
    glm::vec3 _color;
    glm::vec3 _position;
};