#pragma once

#include <vk_types.h>
#include <chunk.h>

class ChunkMesher {
public:
    ChunkMesher(ChunkView& chunk, const std::array<ChunkView, 8>& neighbors) : _chunk(chunk), _chunkNeighbors(neighbors) {}

    std::pair<Mesh, Mesh> generate_mesh();

private:
    ChunkView& _chunk;
    const std::array<ChunkView, 8>& _chunkNeighbors;

    //Mesh _mesh;
    //Mesh _waterMesh;

    std::optional<Block&> get_face_neighbor(int x, int y, int z, FaceDirection face) const;
    bool is_face_visible(int x, int y, int z, FaceDirection face);
    bool is_face_visible_water(int x, int y, int z, FaceDirection face);
    void add_face_to_opaque_mesh(int x, int y, int z, FaceDirection face, Mesh& mesh);
    void add_face_to_water_mesh(int x, int y, int z, FaceDirection face, Mesh& mesh) const;
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    bool is_position_solid(const glm::ivec3& localPos);
    static std::optional<Direction> get_chunk_direction(const glm::ivec3& localPos);

    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};