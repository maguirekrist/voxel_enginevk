
#include "cube_engine.h"
#include "tracy/Tracy.hpp"
#include "camera.h"

CubeEngine::CubeEngine()
{
    create_player();
    refresh_snapshot();
}

CubeEngine::~CubeEngine()
= default;

void CubeEngine::set_player_input(const PlayerInputState& input)
{
    _playerInput = input;
}

void CubeEngine::configure_player(const PlayerPhysicsTuning& tuning, const bool flyModeEnabled)
{
    if (_player == nullptr)
    {
        return;
    }

    _player->set_tuning(tuning);
    _player->set_fly_mode(flyModeEnabled);
    refresh_snapshot();
}

void CubeEngine::update(const float deltaTime)
{
    ZoneScopedN("GameUpdate");
    if (_player == nullptr)
    {
        return;
    }

    _player->tick(deltaTime);
    apply_player_input();
    _player->simulate(deltaTime, _worldCollision);

    _chunkManager.update_player_position(_player->_position);
    _current_chunk = _world.get_chunk(_player->_position);
    _current_block = _world.get_block(_player->_position);

    refresh_snapshot();
}

const GameSnapshot& CubeEngine::snapshot() const
{
    return _snapshot;
}

const Chunk* CubeEngine::get_chunk(const ChunkCoord coord) const
{
    return _chunkManager.get_chunk(coord);
}

const Block* CubeEngine::get_block(const glm::vec3& worldPos) const
{
    return _world.get_block(worldPos);
}

std::optional<RaycastResult> CubeEngine::raycast_target_block(const glm::vec3& origin, const glm::vec3& direction, const float maxDistance)
{
    return Camera::get_target_block(_world, origin, direction, maxDistance);
}

void CubeEngine::apply_block_edit(const BlockEdit& edit)
{
    _chunkManager.enqueue_block_edit(edit);
}

void CubeEngine::regenerate_world()
{
    _chunkManager.regenerate_world();
}

ChunkManager& CubeEngine::chunk_manager()
{
    return _chunkManager;
}

const ChunkManager& CubeEngine::chunk_manager() const
{
    return _chunkManager;
}

void CubeEngine::create_player()
{
    auto player = std::make_unique<PlayerEntity>(GameConfig::DEFAULT_POSITION);
    _player = std::move(player);
}

void CubeEngine::apply_player_input()
{
    _player->apply_input(_playerInput);
    _playerInput = PlayerInputState{};
}

void CubeEngine::refresh_snapshot()
{
    if (_player == nullptr)
    {
        return;
    }

    _snapshot.player = PlayerSnapshot{
        .position = _player->_position,
        .velocity = _player->movement().velocity,
        .facing = _player->body_facing(),
        .cameraTarget = _player->camera_target(),
        .cameraForward = _player->camera_forward(),
        .up = _player->_up,
        .bodyYaw = _player->body_yaw_degrees(),
        .cameraYaw = _player->camera_yaw_degrees(),
        .cameraPitch = _player->camera_pitch_degrees(),
        .grounded = _player->movement().grounded
    };
    _snapshot.currentChunk = _current_chunk != nullptr ? std::optional<ChunkCoord>{_current_chunk->_data->coord} : std::nullopt;
    _snapshot.hasCurrentBlock = _current_block != nullptr;
}

const PlayerEntity* CubeEngine::player() const noexcept
{
    return _player.get();
}
