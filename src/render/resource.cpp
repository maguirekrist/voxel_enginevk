//
// Created by mkrist on 11/19/2024.
//

#include "resource.h"

Resource::Resource(const ResourceBackendContext backend, const Type type, ResourceValue&& value): type(type), backend(backend)
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

Resource::Resource(Resource&& other) noexcept: type(other.type), backend(other.backend), value(other.value)
{
    switch (type) {
    case BUFFER:
        other.value.buffer._buffer = VK_NULL_HANDLE; // Invalidate the moved-from object
        break;
    case IMAGE:
        break;
    }

    other.backend = {};
}

Resource::~Resource()
{
    std::println("Resource::~Resource()");
    switch(type)
    {
    case BUFFER:
        if (value.buffer._buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(backend.allocator, value.buffer._buffer, value.buffer._allocation);
        }
        break;
    case IMAGE:
        if (value.image.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(backend.device, value.image.view, nullptr);
        }
        if (value.image.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(backend.device, value.image.sampler, nullptr);
        }
        if (value.image.image._image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(backend.allocator, value.image.image._image, value.image.image._allocation);
        }
        break;
    }
}
