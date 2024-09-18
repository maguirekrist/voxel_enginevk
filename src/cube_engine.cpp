
#include "cube_engine.h"


void CubeEngine::update()
{
    glm::ivec2 worldCoord = { _player._position.x, _player._position.z };
    _chunkManager.updatePlayerPosition(worldCoord.x, worldCoord.y);
}
