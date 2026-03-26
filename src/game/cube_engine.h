#pragma once

#include <optional>
#include <string_view>

#include "camera.h"
#include "player_input_state.h"
#include "player_entity.h"
#include "world.h"
#include <components/game_object.h>
#include <components/player_input_component.h>
#include <world/chunk_manager.h>

struct PlayerSnapshot
{
    glm::vec3 position{GameConfig::DEFAULT_POSITION};
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{1.0f, 0.0f, 0.0f};
    glm::vec3 cameraTarget{GameConfig::DEFAULT_POSITION};
    glm::vec3 cameraForward{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float bodyYaw{0.0f};
    float cameraYaw{0.0f};
    float cameraPitch{0.0f};
    bool grounded{false};
};

struct GameSnapshot
{
    PlayerSnapshot player{};
    std::optional<ChunkCoord> currentChunk{};
    bool hasCurrentBlock{false};
};

class CubeEngine {
public:
    CubeEngine();
    ~CubeEngine();

    void set_player_input(const PlayerInputState& input);
    void configure_player(const PlayerPhysicsTuning& tuning, const CharacterBodyComponent& body, bool flyModeEnabled);
    void set_player_render_assembly_asset_id(std::string_view assetId);
    void update(float deltaTime);

    [[nodiscard]] const GameSnapshot& snapshot() const;
    [[nodiscard]] const Chunk* get_chunk(ChunkCoord coord) const;
    [[nodiscard]] const Block* get_block(const glm::vec3& worldPos) const;
    [[nodiscard]] std::optional<RaycastResult> raycast_target_block(const glm::vec3& origin, const glm::vec3& direction, float maxDistance);
    void apply_block_edit(const BlockEdit& edit);
    void regenerate_world();
    [[nodiscard]] ChunkManager& chunk_manager();
    [[nodiscard]] const ChunkManager& chunk_manager() const;
    [[nodiscard]] const PlayerEntity* player() const noexcept;

private:
    ChunkManager _chunkManager;
    World _world{ _chunkManager };
    WorldCollision _worldCollision{ _world };
    std::unique_ptr<PlayerEntity> _player;
    PlayerInputState _playerInput{};
    GameSnapshot _snapshot{};

    const Block* _current_block{nullptr};
    const Chunk* _current_chunk{nullptr};

    void create_player();
    void apply_player_input();
    void refresh_snapshot();
};
