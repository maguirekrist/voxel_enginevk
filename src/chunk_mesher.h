#pragma once

#include <vk_types.h>
#include <chunk.h>

class ChunkMesher {
public:
    ChunkMesher(Chunk& chunk, const std::vector<Chunk*>& neighbors) : _chunk(chunk), _chunkNeighbors(neighbors) {}

    void execute()
    {
        generate_mesh();
    }

private:
    Chunk& _chunk;
    const std::vector<Chunk*>& _chunkNeighbors;

    Mesh _mesh;

    void generate_mesh();

    Block* get_face_neighbor(int x, int y, int z, FaceDirection face);
    bool is_face_visible(int x, int y, int z, FaceDirection face);
    void add_face_to_mesh(int x, int y, int z, FaceDirection face);
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    bool is_position_solid(const glm::ivec3& localPos);
    std::optional<Direction> get_chunk_direction(const glm::ivec3& localPos);

    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};