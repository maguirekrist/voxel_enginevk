//
// Created by mkrist on 11/19/2024.
//

#include "resource.h"
#include "vk_engine.h"

Resource::Resource(const Type type, ResourceValue&& value): type(type)
{
    switch (type) {
    case BUFFER:
        new (&this->value.buffer) AllocatedBuffer(value.buffer);
        break;
    case IMAGE:
        new (&this->value.image) ImageResource(value.image);
        break;
    }
}

Resource::Resource(Resource&& other) noexcept: type(other.type), value(other.value)
{
    switch (type) {
    case BUFFER:
        other.value.buffer._buffer = VK_NULL_HANDLE; // Invalidate the moved-from object
        break;
    case IMAGE:
        break;
    }
}

Resource::~Resource()
{
    std::println("Resource::~Resource()");
    switch(type)
    {
    case BUFFER:
        vmaDestroyBuffer(VulkanEngine::instance()._allocator, value.buffer._buffer, value.buffer._allocation);
        break;
    case IMAGE:
        vkDestroyImageView(VulkanEngine::instance()._device, value.image.view, nullptr);
        vkDestroySampler(VulkanEngine::instance()._device, value.image.sampler, nullptr);
        vmaDestroyImage(VulkanEngine::instance()._allocator, value.image.image._image, value.image.image._allocation);
        break;
    }
}
