#pragma once
#include "vk_types.h"

struct ResourceBackendContext
{
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{};
};

struct Resource {
    enum Type {
        BUFFER,
        IMAGE
    } type;

    ResourceBackendContext backend{};

    union ResourceValue{
        AllocatedBuffer buffer;
        ImageResource image;

        ResourceValue() : buffer() {}
        explicit ResourceValue(const AllocatedBuffer& buffer) : buffer(buffer) {}
        explicit ResourceValue(const ImageResource& image) : image(image) {}
    } value;

    Resource(ResourceBackendContext backend, Type type, ResourceValue&& value);
    // Move Constructor
    Resource(Resource&& other) noexcept;

    ~Resource();

    //Delete copy constructor and copy assignment
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
};
