//
// Created by Maguire Krist on 8/23/25.
//

#include "mesh.h"

#include "game/block.h"

std::shared_ptr<Mesh> Mesh::create_cube_mesh()
{
    auto cubeMesh = std::make_shared<Mesh>();

    for (auto face : faceDirections)
    {
        //4 vertices per face
        for (int i = 0; i < 4; i++)
        {
            cubeMesh->_vertices.push_back({ faceVertices[face][i], glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 1.0f) });
        }
    }

    // Define the indices for the cube (two triangles per face)
    cubeMesh->_indices = {
        0, 1, 2, 2, 3, 0,   // Front face
        4, 5, 6, 6, 7, 4,   // Back face
        8, 9, 10, 10, 11, 8, // Left face
        12, 13, 14, 14, 15, 12, // Right face
        16, 17, 18, 18, 19, 16, // Top face
        20, 21, 22, 22, 23, 20  // Bottom face
    };

    return cubeMesh;
}

std::shared_ptr<Mesh> Mesh::create_quad_mesh()
{
    auto quadMesh = std::make_shared<Mesh>();

    // Define the four vertices of the quad in normalized coordinates
    // You can adjust the position values to scale the quad if needed.
    quadMesh->_vertices = {
        { glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f) }, // Bottom-left
        { glm::vec3(0.5f, -0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f) },  // Bottom-right
        { glm::vec3(0.5f, 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f) },   // Top-right
        { glm::vec3(-0.5f, 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f) }   // Top-left
    };

    // Define the indices for the quad (two triangles)
    quadMesh->_indices = {
        0, 1, 2, // First triangle
        2, 3, 0  // Second triangle
    };

    return quadMesh;
}
