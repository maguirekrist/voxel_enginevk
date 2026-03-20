
#include "cube_engine.h"
#include "tracy/Tracy.hpp"

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

void CubeEngine::update(const float deltaTime)
{
    ZoneScopedN("GameUpdate");
    if (_player == nullptr)
    {
        return;
    }

    _player->update(deltaTime);
    apply_player_input();

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
    auto player = std::make_unique<GameObject>(GameConfig::DEFAULT_POSITION);
    auto& playerRef = *player;
    playerRef.Add<PlayerInputComponent>([this](const glm::vec3& pos) -> bool
    {
        const auto block = _world.get_block(pos);
        if (block != nullptr)
        {
            return block->_solid;
        }
        return false;
    });

    _player = std::move(player);
}

void CubeEngine::apply_player_input()
{
    auto& input = _player->Get<PlayerInputComponent>();

    if (_playerInput.lookDeltaX != 0.0f || _playerInput.lookDeltaY != 0.0f)
    {
        input.handle_mouse_move(*_player, _playerInput.lookDeltaX, _playerInput.lookDeltaY);
    }

    if (_playerInput.moveForward)
    {
        input.move_forward(*_player);
    }

    if (_playerInput.moveBackward)
    {
        input.move_backward(*_player);
    }

    if (_playerInput.moveLeft)
    {
        input.move_left(*_player);
    }

    if (_playerInput.moveRight)
    {
        input.move_right(*_player);
    }

    _playerInput.lookDeltaX = 0.0f;
    _playerInput.lookDeltaY = 0.0f;
}

void CubeEngine::refresh_snapshot()
{
    if (_player == nullptr)
    {
        return;
    }

    _snapshot.player = PlayerSnapshot{
        .position = _player->_position,
        .front = _player->_front,
        .up = _player->_up,
        .yaw = _player->_yaw,
        .pitch = _player->_pitch
    };
    _snapshot.currentChunk = _current_chunk != nullptr ? std::optional<ChunkCoord>{_current_chunk->_data->coord} : std::nullopt;
    _snapshot.hasCurrentBlock = _current_block != nullptr;
}
