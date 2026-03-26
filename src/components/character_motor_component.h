#pragma once

#include <glm/vec3.hpp>

#include "game_object.h"

struct CharacterMotorState
{
    glm::vec3 velocity{0.0f};
    bool grounded{false};
    bool jumpQueued{false};
    float jumpBufferTimeRemaining{0.0f};
    float coyoteTimeRemaining{0.0f};
};

struct CharacterMotorTuning
{
    float moveSpeed{4.5f};
    float airControl{0.35f};
    float gravity{24.0f};
    float jumpVelocity{8.5f};
    float maxFallSpeed{30.0f};
};

struct CharacterMotorComponent final : Component
{
    CharacterMotorState state{};
    CharacterMotorTuning tuning{};
    bool flyModeEnabled{false};
};
