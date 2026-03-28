#include "player_entity.h"

#include <algorithm>
#include <cmath>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "game/player_input_state.h"
#include "voxel/voxel_animation_runtime.h"

namespace
{
    constexpr float LookSensitivity = 0.12f;
    constexpr float MinCameraPitch = -65.0f;
    constexpr float MaxCameraPitch = 35.0f;
    constexpr float CollisionStepEpsilon = 0.001f;
    constexpr float JumpBufferDuration = 0.12f;
    constexpr float CoyoteTimeDuration = 0.10f;

    [[nodiscard]] glm::vec3 planar_forward_from_yaw(const float yawDegrees) noexcept
    {
        const float yawRadians = glm::radians(yawDegrees);
        return glm::normalize(glm::vec3(std::cos(yawRadians), 0.0f, std::sin(yawRadians)));
    }
}

PlayerEntity::PlayerEntity(const glm::vec3& position) :
    Entity(position)
{
    Add<CharacterBodyComponent>();
    Add<CharacterMotorComponent>();
    auto& collider = Add<SpatialColliderComponent>();
    collider.valid = true;
    collider.localBounds = AABB{
        .min = glm::vec3(-0.35f, 0.0f, -0.35f),
        .max = glm::vec3(0.35f, 1.8f, 0.35f)
    };
    auto& assemblyComponent = Add<VoxelAssemblyComponent>();
    assemblyComponent.position = position;
    assemblyComponent.placementPolicy = VoxelPlacementPolicy::BottomCenter;
    assemblyComponent.visible = true;
    Add<VoxelAnimationComponent>();
    _front = planar_forward_from_yaw(_cameraYawDegrees);
    update_render_component();
}

void PlayerEntity::apply_input(const PlayerInputState& input)
{
    auto& motor = Get<CharacterMotorComponent>();
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
        motor.state.jumpQueued = true;
        motor.state.jumpBufferTimeRemaining = JumpBufferDuration;
    }
}

void PlayerEntity::simulate(const float deltaTime, const WorldCollision& collision)
{
    auto& motor = Get<CharacterMotorComponent>();
    motor.state.jumpBufferTimeRemaining = std::max(0.0f, motor.state.jumpBufferTimeRemaining - deltaTime);
    motor.state.coyoteTimeRemaining = std::max(0.0f, motor.state.coyoteTimeRemaining - deltaTime);

    const glm::vec3 planarForward = planar_forward_from_yaw(_cameraYawDegrees);
    const glm::vec3 planarRight = glm::normalize(glm::cross(planarForward, _up));
    glm::vec3 desiredMove = (planarRight * _moveIntent.x) + (planarForward * _moveIntent.y);
    if (glm::length(desiredMove) > 0.0001f)
    {
        desiredMove = glm::normalize(desiredMove);
    }

    if (motor.flyModeEnabled)
    {
        const glm::vec3 flyVelocity = (desiredMove * motor.tuning.moveSpeed) + (_up * (_verticalIntent * motor.tuning.moveSpeed));
        motor.state.velocity = flyVelocity;
        _position += motor.state.velocity * deltaTime;
        motor.state.grounded = false;
        if (glm::length(desiredMove) > 0.0001f)
        {
            _bodyYawDegrees = glm::degrees(std::atan2(desiredMove.z, desiredMove.x));
        }

        _yaw = _cameraYawDegrees;
        _pitch = _cameraPitchDegrees;
        _front = camera_forward();
        update_render_component();
        motor.state.jumpQueued = false;
        motor.state.jumpBufferTimeRemaining = 0.0f;
        motor.state.coyoteTimeRemaining = 0.0f;
        return;
    }

    if (motor.state.grounded)
    {
        motor.state.coyoteTimeRemaining = CoyoteTimeDuration;
    }

    const float control = motor.state.grounded ? 1.0f : motor.tuning.airControl;
    motor.state.velocity.x = desiredMove.x * motor.tuning.moveSpeed * control;
    motor.state.velocity.z = desiredMove.z * motor.tuning.moveSpeed * control;

    if ((motor.state.grounded || motor.state.coyoteTimeRemaining > 0.0f) &&
        (motor.state.jumpQueued || motor.state.jumpBufferTimeRemaining > 0.0f))
    {
        motor.state.velocity.y = motor.tuning.jumpVelocity;
        motor.state.grounded = false;
        motor.state.jumpQueued = false;
        motor.state.jumpBufferTimeRemaining = 0.0f;
        motor.state.coyoteTimeRemaining = 0.0f;
    }
    else if (motor.state.jumpBufferTimeRemaining <= 0.0f)
    {
        motor.state.jumpQueued = false;
    }

    if (!motor.state.grounded)
    {
        motor.state.velocity.y = std::max(
            motor.state.velocity.y - (motor.tuning.gravity * deltaTime),
            -motor.tuning.maxFallSpeed);
    }

    motor.state.grounded = false;
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
    const SpatialColliderComponent& collider = Get<SpatialColliderComponent>();
    return collider.world_bounds(_position);
}

glm::vec3 PlayerEntity::body_facing() const noexcept
{
    return planar_forward_from_yaw(_bodyYawDegrees);
}

glm::vec3 PlayerEntity::camera_target() const noexcept
{
    return Get<CharacterBodyComponent>().camera_target(_position);
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
    return Get<CharacterMotorComponent>().state;
}

const CharacterBodyComponent& PlayerEntity::body() const noexcept
{
    return Get<CharacterBodyComponent>();
}

const PlayerPhysicsTuning& PlayerEntity::tuning() const noexcept
{
    return Get<CharacterMotorComponent>().tuning;
}

const glm::vec2& PlayerEntity::move_intent() const noexcept
{
    return _moveIntent;
}

float PlayerEntity::vertical_intent() const noexcept
{
    return _verticalIntent;
}

const VoxelAssemblyComponent& PlayerEntity::assembly_render_component() const
{
    return Get<VoxelAssemblyComponent>();
}

VoxelAnimationComponent* PlayerEntity::animation_component() noexcept
{
    return Has<VoxelAnimationComponent>() ? &Get<VoxelAnimationComponent>() : nullptr;
}

const VoxelAnimationComponent* PlayerEntity::animation_component() const noexcept
{
    return Has<VoxelAnimationComponent>() ? &Get<VoxelAnimationComponent>() : nullptr;
}

void PlayerEntity::set_render_assembly_asset_id(std::string assetId)
{
    auto& assemblyComponent = Get<VoxelAssemblyComponent>();
    assemblyComponent.assetId = std::move(assetId);
    update_render_component();
}

void PlayerEntity::set_animation_controller_asset_id(std::string assetId)
{
    if (!Has<VoxelAnimationComponent>())
    {
        return;
    }

    auto& animation = Get<VoxelAnimationComponent>();
    animation.controllerAssetId = std::move(assetId);
    animation.layerStates.clear();
    animation.currentPose.clear();
    animation.pendingEvents.clear();
    animation.rootMotion = {};
}

void PlayerEntity::set_tuning(const PlayerPhysicsTuning& tuning) noexcept
{
    Get<CharacterMotorComponent>().tuning = tuning;
}

void PlayerEntity::set_body(const CharacterBodyComponent& body) noexcept
{
    Get<CharacterBodyComponent>() = body;
}

void PlayerEntity::set_fly_mode(const bool enabled) noexcept
{
    auto& motor = Get<CharacterMotorComponent>();
    motor.flyModeEnabled = enabled;
    if (enabled)
    {
        motor.state.velocity = glm::vec3(0.0f);
        motor.state.grounded = false;
        motor.state.jumpQueued = false;
        motor.state.jumpBufferTimeRemaining = 0.0f;
        motor.state.coyoteTimeRemaining = 0.0f;
    }
}

bool PlayerEntity::fly_mode_enabled() const noexcept
{
    return Get<CharacterMotorComponent>().flyModeEnabled;
}

void PlayerEntity::update_render_component()
{
    auto& assemblyComponent = Get<VoxelAssemblyComponent>();
    assemblyComponent.position = _position;
    assemblyComponent.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    assemblyComponent.placementPolicy = VoxelPlacementPolicy::BottomCenter;
    assemblyComponent.visible = true;
}

void PlayerEntity::resolve_axis(const WorldCollision& collision, const int axis, const float deltaTime)
{
    auto& motor = Get<CharacterMotorComponent>();
    float* velocityComponent = axis == 0 ? &motor.state.velocity.x : (axis == 1 ? &motor.state.velocity.y : &motor.state.velocity.z);
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
        motor.state.grounded = true;
    }

    *velocityComponent = 0.0f;
}

void PlayerEntity::resolve_displacement_axis(const WorldCollision& collision, const int axis, const float delta)
{
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
}

void PlayerEntity::apply_animation_root_motion(const VoxelAnimationRootMotionSample& sample, const WorldCollision& collision)
{
    if (!sample.valid)
    {
        return;
    }

    const glm::quat bodyRotation = glm::angleAxis(glm::radians(_bodyYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 worldDelta = bodyRotation * sample.translationDeltaLocal;
    resolve_displacement_axis(collision, 0, worldDelta.x);
    resolve_displacement_axis(collision, 2, worldDelta.z);
    resolve_displacement_axis(collision, 1, worldDelta.y);

    const glm::vec3 rotatedForward = sample.rotationDeltaLocal * glm::vec3(1.0f, 0.0f, 0.0f);
    _bodyYawDegrees += glm::degrees(std::atan2(rotatedForward.z, rotatedForward.x));
    consume_voxel_animation_root_motion(Get<VoxelAnimationComponent>());
    update_render_component();
}
