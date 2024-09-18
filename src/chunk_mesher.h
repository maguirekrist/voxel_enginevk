#pragma once

#include "chunk_manager.h"
#include <vk_types.h>
#include <world.h>

class ChunkMesher {
public:
    ChunkMesher(Chunk& chunk, const std::array<Chunk*, 8>& neighbors) : _chunk(chunk), _chunkNeighbors(neighbors) {}

    void execute()
    {
        generate_mesh();
    }

private:
    Chunk& _chunk;
    const std::array<Chunk*, 8>& _chunkNeighbors;

    Mesh _mesh;

    void generate_mesh();

    Block* get_face_neighbor(const Block& block, FaceDirection face);
    bool is_face_visible(const Block& block, FaceDirection face);
    void add_face_to_mesh(const Block& block, FaceDirection face);
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    bool is_position_solid(const glm::ivec3& localPos);
    std::optional<Direction> get_chunk_direction(const glm::ivec3& localPos);

    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};