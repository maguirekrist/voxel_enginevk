
#include "cube_engine.h"
#include "tracy/Tracy.hpp"
#include "scenes/game_scene.h"

CubeEngine::CubeEngine(GameScene& scene) :
    _scene(scene),
    _current_block(),
    _current_chunk()
{
}

CubeEngine::~CubeEngine()
= default;

void CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    if (_scene._player == nullptr)
    {
        return;
    }

    _chunkManager.update_player_position(static_cast<int>(_scene._player->_position.x), static_cast<int>(_scene._player->_position.z));
    _current_chunk = _world.get_chunk(_scene._player->_position);
    _current_block = _world.get_block(_scene._player->_position);
}
