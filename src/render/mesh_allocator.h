#ifndef MESH_ALLOCATOR_H
#define MESH_ALLOCATOR_H

#include "constants.h"
#include "vk_types.h"

struct MeshAllocation;

struct MeshSlot
{
    VkDeviceSize vertex_offset;
    VkDeviceSize index_offset;
};

class MeshAllocator {
public:

    explicit MeshAllocator(VmaAllocator allocator);
    ~MeshAllocator();

    MeshAllocation acquire();
    void free(MeshAllocation allocation);

    AllocatedBuffer m_indexBuffer{};
    AllocatedBuffer m_vertexBuffer{};
private:
    inline static constexpr VkDeviceSize CAPACITY = GameConfig::MAXIMUM_CHUNKS * 2;
    std::vector<int32_t> m_free_list;
    VmaAllocator m_allocator;

    inline static constexpr VkDeviceSize VERTEX_SLAB_SIZE = 200000; //2 mb
    inline static constexpr VkDeviceSize INDEX_SLAB_SIZE = 200000; //2 mb

    inline static constexpr VkDeviceSize VERTEX_BUFFER_SIZE = VERTEX_SLAB_SIZE * CAPACITY;
    inline static constexpr VkDeviceSize INDEX_BUFFER_SIZE = INDEX_SLAB_SIZE * CAPACITY;

    [[nodiscard]] MeshSlot get_slot(size_t index) const;
};

struct MeshAllocation
{
    MeshSlot slot{};
    int32_t slot_index = -1;
    uint32_t gen = 0;
    VkDeviceSize slab_size = 0;
    MeshAllocator* allocator = nullptr;
};


#endif //MESH_ALLOCATOR_H
