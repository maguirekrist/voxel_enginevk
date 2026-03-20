#pragma once

#include <optional>

#include "camera.h"
#include "world.h"
#include <components/game_object.h>
#include <components/player_input_component.h>
#include <world/chunk_manager.h>

struct PlayerInputState
{
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    float lookDeltaX{0.0f};
    float lookDeltaY{0.0f};
};

struct PlayerSnapshot
{
    glm::vec3 position{GameConfig::DEFAULT_POSITION};
    glm::vec3 front{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float yaw{0.0f};
    float pitch{0.0f};
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
    void update(float deltaTime);

    [[nodiscard]] const GameSnapshot& snapshot() const;
    [[nodiscard]] const Chunk* get_chunk(ChunkCoord coord) const;
    [[nodiscard]] const Block* get_block(const glm::vec3& worldPos) const;
    [[nodiscard]] std::optional<RaycastResult> raycast_target_block(float maxDistance);
    void apply_block_edit(const BlockEdit& edit);
    [[nodiscard]] ChunkManager& chunk_manager();
    [[nodiscard]] const ChunkManager& chunk_manager() const;

private:
    ChunkManager _chunkManager;
    World _world{ _chunkManager };
    std::unique_ptr<GameObject> _player;
    PlayerInputState _playerInput{};
    GameSnapshot _snapshot{};

    const Block* _current_block{nullptr};
    const Chunk* _current_chunk{nullptr};

    void create_player();
    void apply_player_input();
    void refresh_snapshot();
};
