
#include "cube_engine.h"

#include "tracy/Tracy.hpp"

CubeEngine::CubeEngine()
{
    
}

void CubeEngine::cleanup()
{
    _chunkManager.cleanup();
}

std::optional<WorldUpdate> CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    glm::ivec2 worldCoord = { _player._position.x, _player._position.z };
    return _chunkManager.update_player_position(worldCoord.x, worldCoord.y);
}
