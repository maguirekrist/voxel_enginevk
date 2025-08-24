//
// Created by Maguire Krist on 8/23/25.
//

#ifndef MESH_H
#define MESH_H
#include "mesh_allocator.h"
#include "vk_vertex.h"


struct Mesh {
    std::vector<Vertex> _vertices;
    std::vector<uint32_t> _indices;
    MeshAllocation _allocation{};

    std::atomic_bool _isActive = false;

    static std::shared_ptr<Mesh> create_cube_mesh();
    static std::shared_ptr<Mesh> create_quad_mesh();

    Mesh(): _allocation()
    {
        //std::println("Mesh::Mesh()");
    }

    ~Mesh()
    {
        //std::println("Mesh::~Mesh()");

    };
};




#endif //MESH_H
