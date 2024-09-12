#pragma once

#include <vk_types.h>
#include <world.h>

class ChunkMesher {
public:
    ChunkMesher(Chunk* chunk, World* world) : _chunk(chunk), _world(world) {
        generate_mesh(chunk);
    }

private:
    Chunk* _chunk;
    World* _world;

    void generate_mesh(Chunk* chunk);

    Block* get_face_neighbor(const Block& block, FaceDirection face);
    bool is_face_visible(const Block& block, FaceDirection face);
    void add_face_to_mesh(const Block& block, FaceDirection face);
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};