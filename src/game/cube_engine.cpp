
#include "cube_engine.h"

#include "tracy/Tracy.hpp"

CubeEngine::CubeEngine() :
    _player([this](const glm::vec3& pos) -> bool
    {
        const auto block = _world.get_block(pos);
        return block->_solid;
    }),
    _current_block(),
    _current_chunk()
{
}

CubeEngine::~CubeEngine()
= default;

void CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    _chunkManager.update_player_position(static_cast<int>(_player._position.x), static_cast<int>(_player._position.z));
    _current_chunk = _world.get_chunk(_player._position);
    _current_block = _world.get_block(_player._position);
}
