
#pragma once
#include <vk_types.h>

enum FaceDirection {
    FRONT_FACE = 0,
    BACK_FACE = 1,
    RIGHT_FACE = 2,
    LEFT_FACE = 3,
    TOP_FACE = 4,
    BOTTOM_FACE = 5
};

enum QuadCorners {
    BOTTOM_LEFT = 0,
    BOTTOM_RIGHT = 1,
    TOP_RIGHT = 2,
    TOP_LEFT = 3
};

namespace Colors {
    constexpr glm::vec3 red{ 1.0f, 0.0f, 0.0f };
    constexpr glm::vec3 green{ 0.0f, 1.0f, 0.0f };
    constexpr glm::vec3 blue{ 0.0f, 0.0f, 1.0f };
    constexpr glm::vec3 purple{ 1.0f, 0.0f, 1.0f };
    constexpr glm::vec3 yellow{ 1.0f, 1.0f, 0.0f };
    constexpr glm::vec3 lightBlue{ 0.0f, 1.0f, 1.0f };
};

constexpr FaceDirection faceDirections[6] = { FRONT_FACE, BACK_FACE, RIGHT_FACE, LEFT_FACE, TOP_FACE, BOTTOM_FACE };
constexpr glm::vec3 faceColors[6] = { Colors::red, Colors::green, Colors::blue, Colors::purple, Colors::yellow, Colors::lightBlue };

constexpr int faceOffsetX[] = { 0,  0, -1,  1,  0,  0 };
constexpr int faceOffsetY[] = { 0,  0,  0,  0,  1, -1 };
constexpr int faceOffsetZ[] = { 1, -1,  0,  0,  0,  0 };

//up an down
constexpr glm::ivec3 Side1Offsets[6][4] = {
    // Front face vertices (0, 1, 2, 3)
    { {0, -1, 1}, {0, -1, 1}, {0,  1, 1}, {0,  1, 1} },
    // Back face vertices (4, 5, 6, 7)
    { {0, -1, -1}, {0, -1, -1}, {0,  1, -1}, {0,  1, -1} },
    // Right face vertices (8, 9, 10, 11)
    { {-1, -1, 0}, {-1, -1, 0}, { -1, 1, 0}, {-1, 1, 0} },
    // Left face vertices (12, 13, 14, 15)
    { {1, -1, 0}, {1, -1, 0}, { 1, 1, 0}, {1, 1, 0} },
    // Top face vertices (16, 17, 18, 19)
    { {0, 1,  -1}, {0, 1, -1}, {0, 1, 1}, {0, 1,  1} },
    // Bottom face vertices (20, 21, 22, 23)
    { {0, 0,  0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} }
};


//left and right
constexpr glm::ivec3 Side2Offsets[6][4] = {
    // Front face vertices (0, 1, 2, 3)
    { {-1, 0, 1}, { 1, 0, 1}, { 1, 0, 1}, {-1, 0, 1} },
    // Back face vertices (4, 5, 6, 7)
    { {1, 0, -1}, {-1, 0, -1}, {-1, 0, -1}, {1, 0, -1} },
    // right face vertices (8, 9, 10, 11)
    { {-1, 0, -1}, {-1, 0, 1}, {-1, 0, 1}, {-1, 0, -1} },
    // left face vertices (12, 13, 14, 15)
    { {1, 0, 1}, {1, 0, -1}, {1, 0, -1}, {1, 0, 1} },
    // Top face vertices (16, 17, 18, 19)
    { { 1, 1, 0}, { -1, 1, 0}, { -1, 1, 0}, { 1, 1, 0} },
    // Bottom face vertices (20, 21, 22, 23)
    { {0, 0, 0}, { 0, 0, 0}, { 0, 0, 0}, {0, 0, 0} }
};

//All have to be in order of bottom left, bottom-right, top-right, top-left
constexpr glm::ivec3 CornerOffsets[6][4] = {
    // Front face vertices (0, 1, 2, 3)
    { {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0} },
    // Back face vertices (4, 5, 6, 7)
    { {1, -1, -1}, { -1, -1, -1}, { -1,  1, -1}, { 1,  1, -1} },
    // right face vertices (8, 9, 10, 11)
    { {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1} },
    // left face vertices (12, 13, 14, 15)
    { {1, 1, 1}, { 1, 1, -1 }, { 1, -1, -1}, { 1, -1, 1 } },
    // Top face vertices (16, 17, 18, 19)
    { {1,  1,  -1}, { -1,  1,  -1}, { -1, 1, 1 }, { 1,  1, 1} },
    // Bottom face vertices (20, 21, 22, 23)
    { {0,  0,  0}, { 0,  0,  0}, { 0,  0, 0}, {0, 0, 0} }
};

//y-up coordinate system vertices, positive-x is left, positive-z is forward.
constexpr glm::vec3 faceVertices[6][4] = {
    // FRONT_FACE
    { {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1} }, //z is front
    // BACK_FACE
    { {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0} }, //z is all 0 because it's back
    // RIGHT_FACE
    { {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0} }, //left is all 0 x becaue  
    // LEFT_FACE
    { {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1} }, //this is left face
    // TOP_FACE
    { {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {1, 1, 1} }, //1, 1, 1 is the top-left-front vertex of the cube
    // BOTTOM_FACE
    { {1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {1, 0, 1} } 
};

struct Block {
    bool _solid = false;
    glm::vec3 _color;
    glm::vec3 _position;
};