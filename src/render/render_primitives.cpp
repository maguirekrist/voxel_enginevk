//
// Created by Maguire Krist on 8/25/25.
//

#include "render_primitives.h"
#include "vk_engine.h"

RenderObject::~RenderObject()
{
    if (mesh != nullptr && mesh->allocation)
    {
        VulkanEngine::instance()._meshManager.enqueue_unload(mesh->allocation);
    }
}