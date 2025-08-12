#pragma once

#include <vk_types.h>
#include <game/chunk.h>

class ChunkMesher {
public:
    ChunkMesher(const std::shared_ptr<const ChunkData>& chunk, std::optional<std::array<std::shared_ptr<const ChunkData>, 8>>& neighbors) : _chunk(chunk), _chunkNeighbors(neighbors) {}

    std::shared_ptr<ChunkMeshData> generate_mesh();

private:
    std::shared_ptr<const ChunkData>_chunk;
    std::optional<std::array<std::shared_ptr<const ChunkData>, 8>>& _chunkNeighbors;

    //Mesh _mesh;
    //Mesh _waterMesh;

    std::optional<const Block> get_face_neighbor(int x, int y, int z, FaceDirection face) const;
    bool is_face_visible(int x, int y, int z, FaceDirection face);
    bool is_face_visible_water(int x, int y, int z, FaceDirection face);
    void add_face_to_opaque_mesh(int x, int y, int z, FaceDirection face, const std::shared_ptr<Mesh>& mesh);
    void add_face_to_water_mesh(int x, int y, int z, FaceDirection face, const std::shared_ptr<Mesh>& mesh) const;
    float calculate_vertex_ao(glm::ivec3 cubePos, FaceDirection face, int vertex);
    bool is_position_solid(const glm::ivec3& localPos);
    static std::optional<Direction> get_chunk_direction(const glm::ivec3& localPos);

    void propagate_sunlight();
    void propagate_pointlight(glm::vec3 lightPos, int lightLevel);

};