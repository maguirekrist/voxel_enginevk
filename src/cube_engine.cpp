
#include "cube_engine.h"


void CubeEngine::update()
{
    //For now, this function is responsible for checking wether or not we need to mesh new chunks.
    //Update will do whatever logic the game needs to ensure that the _renderObjects array is what we want to render afer
    //handling input from SDL.

    _chunkManager.updatePlayerPosition(_player._position.x, _player._position.z);
    //_chunkManager.printLoadedChunks();
    //load_chunks();
}


// void CubeEngine::load_chunks()
// {
//     //Center in Chunk-Space
//     //glm::ivec2 chunkCord = glm::floor(glm::vec2{ _player._position.x / CHUNK_SIZE, _player._position.z / CHUNK_SIZE });
//     auto playerChunkCoord = World::get_chunk_coordinates(_player._position);

//     for (int x = -DEFAULT_VIEW_DISTANCE; x <= DEFAULT_VIEW_DISTANCE; x++)
//     {
//         for(int y = -DEFAULT_VIEW_DISTANCE; y <= DEFAULT_VIEW_DISTANCE; y++)
//         {
//             auto xStart = (playerChunkCoord.x + x) * CHUNK_SIZE;
//             auto yStart = (playerChunkCoord.y + y) * CHUNK_SIZE;
//             _world.generate_chunk(xStart, yStart);
//         }
//     }

//     //mesh the any new chunks.
//     for (auto& [key, chunk] : _world._chunkMap)
//     {
//         _chunkMesher.generate_mesh(chunk.get());
//     }

//     //last-step, render objects

// }