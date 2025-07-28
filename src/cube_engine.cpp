
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
    //For now, this function is responsible for checking wether or not we need to mesh new chunks.
    //Update will do whatever logic the game needs to ensure that the _renderObjects array is what we want to render afer
    //handling input from SDL.
    
    glm::ivec2 worldCoord = { _player._position.x, _player._position.z };

    //fmt::println("Player position: x{}, y {}, z{}", _player._position.x, _player._position.y, _player._position.z);

    _chunkManager.update_player_position(worldCoord.x, worldCoord.y);
}
