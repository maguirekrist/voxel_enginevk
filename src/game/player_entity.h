#pragma once

#include <string>

#include "entity.h"
#include "game/world_collision.h"
#include "components/voxel_assembly_component.h"
#include "components/voxel_model_component.h"

struct PlayerInputState;

struct PlayerMovementState
{
    glm::vec3 velocity{0.0f};
    bool grounded{false};
    bool jumpQueued{false};
    float jumpBufferTimeRemaining{0.0f};
    float coyoteTimeRemaining{0.0f};
};

struct PlayerBodyDef
{
    glm::vec3 halfExtents{0.35f, 0.9f, 0.35f};
    float height{1.8f};
    glm::vec3 cameraTargetOffset{0.0f, 1.4f, 0.0f};
};

struct PlayerPhysicsTuning
{
    float moveSpeed{4.5f};
    float airControl{0.35f};
    float gravity{24.0f};
    float jumpVelocity{8.5f};
    float maxFallSpeed{30.0f};
};

class PlayerEntity final : public Entity
{
public:
    explicit PlayerEntity(const glm::vec3& position);

    void apply_input(const PlayerInputState& input);
    void simulate(float deltaTime, const WorldCollision& collision);
    void tick(float deltaTime) override;
    void set_tuning(const PlayerPhysicsTuning& tuning) noexcept;
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
    [[nodiscard]] const PlayerBodyDef& body() const noexcept;
    [[nodiscard]] const PlayerPhysicsTuning& tuning() const noexcept;
    [[nodiscard]] const VoxelModelComponent& render_component() const;
    [[nodiscard]] const VoxelAssemblyComponent& assembly_render_component() const;
    void set_render_assembly_asset_id(std::string assetId);

private:
    void update_render_component();
    void resolve_axis(const WorldCollision& collision, int axis, float deltaTime);

    PlayerMovementState _movement{};
    PlayerBodyDef _body{};
    PlayerPhysicsTuning _tuning{};
    glm::vec2 _moveIntent{0.0f};
    float _verticalIntent{0.0f};
    float _cameraYawDegrees{0.0f};
    float _cameraPitchDegrees{-18.0f};
    float _bodyYawDegrees{0.0f};
    bool _flyModeEnabled{false};
};
