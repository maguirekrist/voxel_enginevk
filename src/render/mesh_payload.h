//
// Created by Maguire Krist on 8/23/25.
//

#ifndef MESH_H
#define MESH_H

#include "mesh_allocator.h"
#include "vk_vertex.h"

struct MeshPayload {
    std::vector<Vertex> _vertices;
    std::vector<uint32_t> _indices;

    std::shared_ptr<MeshRef> _mesh = nullptr;

    static std::shared_ptr<MeshPayload> create_cube_mesh();
    static std::shared_ptr<MeshPayload> create_quad_mesh();
};




#endif //MESH_H
