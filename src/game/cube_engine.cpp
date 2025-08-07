
#include "cube_engine.h"

#include "tracy/Tracy.hpp"

CubeEngine::CubeEngine()
{
    
}

CubeEngine::~CubeEngine()
{
}

void CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    _chunkManager.update_player_position(_player._position.x, _player._position.z);
}
