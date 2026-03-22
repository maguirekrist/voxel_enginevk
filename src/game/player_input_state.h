#pragma once

struct PlayerInputState
{
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool moveUp{false};
    bool moveDown{false};
    bool jumpPressed{false};
    float lookDeltaX{0.0f};
    float lookDeltaY{0.0f};
};
