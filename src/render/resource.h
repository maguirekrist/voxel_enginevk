#pragma once

#include "vk_engine.h"


struct Resource {
    enum Type {
        BUFFER,
        IMAGE
    } type;

    union ResourceValue{
        AllocatedBuffer buffer;
        ImageResource image;

        ResourceValue() : buffer() {}
        explicit ResourceValue(const AllocatedBuffer& buffer) : buffer(buffer) {}
        explicit ResourceValue(const ImageResource& image) : image(image) {}
    } value;

    Resource(const Type type, ResourceValue&& value) : type(type) {
        switch (type) {
            case BUFFER:
                new (&this->value.buffer) AllocatedBuffer(value.buffer);
            break;
            case IMAGE:
                new (&this->value.image) ImageResource(value.image);
            break;
        }
    }

    // Move Constructor
    Resource(Resource&& other) noexcept : type(other.type), value(other.value) {
        switch (type) {
            case BUFFER:
                other.value.buffer._buffer = VK_NULL_HANDLE; // Invalidate the moved-from object
                break;
            case IMAGE:
                break;
        }
    }

    ~Resource()
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

    //Delete copy constructor and copy assignment
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};

