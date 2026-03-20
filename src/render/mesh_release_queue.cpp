#include "mesh_release_queue.h"

#include <utils/blockingconcurrentqueue.h>

#include "mesh.h"

namespace
{
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> g_meshReleaseQueue;
}

void render::enqueue_mesh_release(std::shared_ptr<Mesh> mesh)
{
    if (mesh == nullptr)
    {
        return;
    }

    g_meshReleaseQueue.enqueue(std::move(mesh));
}

bool render::try_dequeue_mesh_release(std::shared_ptr<Mesh>& mesh)
{
    return g_meshReleaseQueue.try_dequeue(mesh);
}
