
#include "cube_engine.h"

#include "tracy/Tracy.hpp"

CubeEngine::CubeEngine()
{
    
}

void CubeEngine::cleanup()
{
    _chunkManager.cleanup();
}

void CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    glm::ivec2 worldCoord = { _player._position.x, _player._position.z };
    _chunkManager.poll_world_update();
    _chunkManager.update_player_position(worldCoord.x, worldCoord.y);
}
