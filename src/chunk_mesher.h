#pragma once

#include "chunk_manager.h"
#include <vk_types.h>
#include <world.h>

class ChunkMesher {
public:
    ChunkMesher(Chunk* chunk, ChunkManager* manager) : _chunk(chunk), _manager(manager) {
    }

    void execute()
    {
        generate_mesh(_chunk);
    }

private:
    Chunk* _chunk;
    ChunkManager* _manager;

    Mesh _mesh;

    void generate_mesh(Chunk* chunk);

    Block* get_face_neighbor(const Block& block, FaceDirection face);
    bool is_face_visible(const Block& block, FaceDirection face);
    void add_face_to_mesh(const Block& block, FaceDirection face);
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    bool is_position_solid(glm::ivec3 localPos);

    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};