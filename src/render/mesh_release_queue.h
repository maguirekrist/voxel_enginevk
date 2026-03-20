#pragma once

#include <memory>

struct Mesh;

namespace render
{
    void enqueue_mesh_release(std::shared_ptr<Mesh> mesh);
    bool try_dequeue_mesh_release(std::shared_ptr<Mesh>& mesh);
}
