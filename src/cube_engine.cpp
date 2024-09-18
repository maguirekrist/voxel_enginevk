
#include "cube_engine.h"

#include "tracy/Tracy.hpp"

void CubeEngine::update()
{
    ZoneScopedN("GameUpdate");
    //For now, this function is responsible for checking wether or not we need to mesh new chunks.
    //Update will do whatever logic the game needs to ensure that the _renderObjects array is what we want to render afer
    //handling input from SDL.
    
    glm::ivec2 worldCoord = { _player._position.x, _player._position.z };
    _chunkManager.updatePlayerPosition(worldCoord.x, worldCoord.y);
}
