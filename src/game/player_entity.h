#pragma once

#include <string>

#include "components/character_body_component.h"
#include "components/character_motor_component.h"
#include "components/spatial_collider_component.h"
#include "components/voxel_animation_component.h"
#include "entity.h"
#include "game/world_collision.h"
#include "components/voxel_assembly_component.h"

struct PlayerInputState;

using PlayerMovementState = CharacterMotorState;
using PlayerPhysicsTuning = CharacterMotorTuning;

class PlayerEntity final : public Entity
{
public:
    explicit PlayerEntity(const glm::vec3& position);

    void apply_input(const PlayerInputState& input);
    void simulate(float deltaTime, const WorldCollision& collision);
    void tick(float deltaTime) override;
    void set_tuning(const PlayerPhysicsTuning& tuning) noexcept;
    void set_body(const CharacterBodyComponent& body) noexcept;
    void set_fly_mode(bool enabled) noexcept;
    [[nodiscard]] bool fly_mode_enabled() const noexcept;

    [[nodiscard]] AABB world_bounds() const noexcept;
    [[nodiscard]] glm::vec3 body_facing() const noexcept;
    [[nodiscard]] glm::vec3 camera_target() const noexcept;
    [[nodiscard]] glm::vec3 camera_forward() const noexcept;
    [[nodiscard]] float body_yaw_degrees() const noexcept;
    [[nodiscard]] float camera_yaw_degrees() const noexcept;
    [[nodiscard]] float camera_pitch_degrees() const noexcept;

    [[nodiscard]] const PlayerMovementState& movement() const noexcept;
    [[nodiscard]] const CharacterBodyComponent& body() const noexcept;
    [[nodiscard]] const PlayerPhysicsTuning& tuning() const noexcept;
    [[nodiscard]] const glm::vec2& move_intent() const noexcept;
    [[nodiscard]] float vertical_intent() const noexcept;
    [[nodiscard]] const VoxelAssemblyComponent& assembly_render_component() const;
    [[nodiscard]] VoxelAnimationComponent* animation_component() noexcept;
    [[nodiscard]] const VoxelAnimationComponent* animation_component() const noexcept;
    void set_render_assembly_asset_id(std::string assetId);
    void set_animation_controller_asset_id(std::string assetId);
    void apply_animation_root_motion(const VoxelAnimationRootMotionSample& sample, const WorldCollision& collision);

private:
    void update_render_component();
    void resolve_axis(const WorldCollision& collision, int axis, float deltaTime);
    void resolve_displacement_axis(const WorldCollision& collision, int axis, float delta);

    glm::vec2 _moveIntent{0.0f};
    float _verticalIntent{0.0f};
    float _cameraYawDegrees{0.0f};
    float _cameraPitchDegrees{-18.0f};
    float _bodyYawDegrees{0.0f};
};
