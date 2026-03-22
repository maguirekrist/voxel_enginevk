#include "player_entity.h"

#include <algorithm>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "game/player_input_state.h"

namespace
{
    constexpr float LookSensitivity = 0.12f;
    constexpr float MinCameraPitch = -65.0f;
    constexpr float MaxCameraPitch = 35.0f;
    constexpr float CollisionStepEpsilon = 0.001f;

    [[nodiscard]] glm::vec3 planar_forward_from_yaw(const float yawDegrees) noexcept
    {
        const float yawRadians = glm::radians(yawDegrees);
        return glm::normalize(glm::vec3(std::cos(yawRadians), 0.0f, std::sin(yawRadians)));
    }
}

PlayerEntity::PlayerEntity(const glm::vec3& position) :
    Entity(position)
{
    auto& voxelComponent = Add<VoxelModelComponent>();
    voxelComponent.assetId = "player";
    voxelComponent.position = position;
    voxelComponent.visible = true;
    _front = planar_forward_from_yaw(_cameraYawDegrees);
    update_render_component();
}

void PlayerEntity::apply_input(const PlayerInputState& input)
{
    const float forwardAxis = (input.moveForward ? 1.0f : 0.0f) - (input.moveBackward ? 1.0f : 0.0f);
    const float strafeAxis = (input.moveRight ? 1.0f : 0.0f) - (input.moveLeft ? 1.0f : 0.0f);
    _verticalIntent = (input.moveUp ? 1.0f : 0.0f) - (input.moveDown ? 1.0f : 0.0f);
    _moveIntent = glm::vec2(strafeAxis, forwardAxis);
    if (glm::length(_moveIntent) > 1.0f)
    {
        _moveIntent = glm::normalize(_moveIntent);
    }

    _cameraYawDegrees += input.lookDeltaX * LookSensitivity;
    _cameraPitchDegrees = std::clamp(
        _cameraPitchDegrees - (input.lookDeltaY * LookSensitivity),
        MinCameraPitch,
        MaxCameraPitch);

    if (input.jumpPressed)
    {
        _movement.jumpQueued = true;
    }
}

void PlayerEntity::simulate(const float deltaTime, const WorldCollision& collision)
{
    const glm::vec3 planarForward = planar_forward_from_yaw(_cameraYawDegrees);
    const glm::vec3 planarRight = glm::normalize(glm::cross(planarForward, _up));
    glm::vec3 desiredMove = (planarRight * _moveIntent.x) + (planarForward * _moveIntent.y);
    if (glm::length(desiredMove) > 0.0001f)
    {
        desiredMove = glm::normalize(desiredMove);
    }

    if (_flyModeEnabled)
    {
        const glm::vec3 flyVelocity = (desiredMove * _tuning.moveSpeed) + (_up * (_verticalIntent * _tuning.moveSpeed));
        _movement.velocity = flyVelocity;
        _position += _movement.velocity * deltaTime;
        _movement.grounded = false;
        if (glm::length(desiredMove) > 0.0001f)
        {
            _bodyYawDegrees = glm::degrees(std::atan2(desiredMove.z, desiredMove.x));
        }

        _yaw = _cameraYawDegrees;
        _pitch = _cameraPitchDegrees;
        _front = camera_forward();
        update_render_component();
        _movement.jumpQueued = false;
        return;
    }

    const float control = _movement.grounded ? 1.0f : _tuning.airControl;
    _movement.velocity.x = desiredMove.x * _tuning.moveSpeed * control;
    _movement.velocity.z = desiredMove.z * _tuning.moveSpeed * control;

    if (_movement.grounded && _movement.jumpQueued)
    {
        _movement.velocity.y = _tuning.jumpVelocity;
        _movement.grounded = false;
    }
    _movement.jumpQueued = false;

    if (!_movement.grounded)
    {
        _movement.velocity.y = std::max(_movement.velocity.y - (_tuning.gravity * deltaTime), -_tuning.maxFallSpeed);
    }

    _movement.grounded = false;
    resolve_axis(collision, 0, deltaTime);
    resolve_axis(collision, 2, deltaTime);
    resolve_axis(collision, 1, deltaTime);

    if (glm::length(desiredMove) > 0.0001f)
    {
        _bodyYawDegrees = glm::degrees(std::atan2(desiredMove.z, desiredMove.x));
    }

    _yaw = _cameraYawDegrees;
    _pitch = _cameraPitchDegrees;
    _front = camera_forward();
    update_render_component();
}

void PlayerEntity::tick(const float deltaTime)
{
    GameObject::update(deltaTime);
    (void)deltaTime;
}

AABB PlayerEntity::world_bounds() const noexcept
{
    return AABB{
        .min = _position + glm::vec3(-_body.halfExtents.x, 0.0f, -_body.halfExtents.z),
        .max = _position + glm::vec3(_body.halfExtents.x, _body.height, _body.halfExtents.z)
    };
}

glm::vec3 PlayerEntity::body_facing() const noexcept
{
    return planar_forward_from_yaw(_bodyYawDegrees);
}

glm::vec3 PlayerEntity::camera_target() const noexcept
{
    return _position + _body.cameraTargetOffset;
}

glm::vec3 PlayerEntity::camera_forward() const noexcept
{
    const float yawRadians = glm::radians(_cameraYawDegrees);
    const float pitchRadians = glm::radians(_cameraPitchDegrees);
    return glm::normalize(glm::vec3(
        std::cos(yawRadians) * std::cos(pitchRadians),
        std::sin(pitchRadians),
        std::sin(yawRadians) * std::cos(pitchRadians)));
}

float PlayerEntity::body_yaw_degrees() const noexcept
{
    return _bodyYawDegrees;
}

float PlayerEntity::camera_yaw_degrees() const noexcept
{
    return _cameraYawDegrees;
}

float PlayerEntity::camera_pitch_degrees() const noexcept
{
    return _cameraPitchDegrees;
}

const PlayerMovementState& PlayerEntity::movement() const noexcept
{
    return _movement;
}

const PlayerBodyDef& PlayerEntity::body() const noexcept
{
    return _body;
}

const PlayerPhysicsTuning& PlayerEntity::tuning() const noexcept
{
    return _tuning;
}

const VoxelModelComponent& PlayerEntity::render_component() const
{
    return Get<VoxelModelComponent>();
}

void PlayerEntity::set_fly_mode(const bool enabled) noexcept
{
    _flyModeEnabled = enabled;
    if (enabled)
    {
        _movement.velocity = glm::vec3(0.0f);
        _movement.grounded = false;
        _movement.jumpQueued = false;
    }
}

bool PlayerEntity::fly_mode_enabled() const noexcept
{
    return _flyModeEnabled;
}

void PlayerEntity::update_render_component()
{
    auto& voxelComponent = Get<VoxelModelComponent>();
    voxelComponent.position = _position;
    voxelComponent.rotation = glm::angleAxis(glm::radians(_bodyYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    voxelComponent.visible = true;
}

void PlayerEntity::resolve_axis(const WorldCollision& collision, const int axis, const float deltaTime)
{
    float* velocityComponent = axis == 0 ? &_movement.velocity.x : (axis == 1 ? &_movement.velocity.y : &_movement.velocity.z);
    float delta = *velocityComponent * deltaTime;
    if (std::abs(delta) <= 0.000001f)
    {
        return;
    }

    glm::vec3 step{};
    step[axis] = delta;
    const AABB candidate = world_bounds().moved(step);
    if (!collision.intersects_solid(candidate))
    {
        _position += step;
        return;
    }

    const float direction = delta > 0.0f ? 1.0f : -1.0f;
    const float distance = std::abs(delta);
    float traveled = 0.0f;
    const float increment = 0.05f;

    while (traveled + increment < distance)
    {
        traveled += increment;
        glm::vec3 partialStep{};
        partialStep[axis] = direction * traveled;
        if (collision.intersects_solid(world_bounds().moved(partialStep)))
        {
            traveled -= increment;
            break;
        }
    }

    glm::vec3 resolvedStep{};
    resolvedStep[axis] = direction * std::max(0.0f, traveled - CollisionStepEpsilon);
    _position += resolvedStep;

    if (axis == 1 && direction < 0.0f)
    {
        _movement.grounded = true;
    }

    *velocityComponent = 0.0f;
}
