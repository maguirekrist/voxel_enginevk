#ifndef MESH_ALLOCATOR_H
#define MESH_ALLOCATOR_H

#include "constants.h"
#include "vk_types.h"

struct MeshAllocation;

struct MeshAllocatorConfig
{
    size_t slotCapacity{static_cast<size_t>((maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE) * 2) + 128)};
    VkDeviceSize vertexSlabSize{1048576};
    VkDeviceSize indexSlabSize{262144};
};

struct MeshSlot
{
    VkDeviceSize vertex_offset;
    VkDeviceSize index_offset;
};

class MeshAllocator {
public:
    explicit MeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config = {});
    ~MeshAllocator();

    MeshAllocation acquire();
    void free(MeshAllocation allocation);
    void reconfigure(MeshAllocatorConfig config);
    [[nodiscard]] bool can_reconfigure() const noexcept;
    [[nodiscard]] const MeshAllocatorConfig& config() const noexcept;

    AllocatedBuffer m_indexBuffer{};
    AllocatedBuffer m_vertexBuffer{};
private:
    std::vector<int32_t> m_free_list;
    VmaAllocator m_allocator;
    MeshAllocatorConfig m_config{};

    [[nodiscard]] MeshSlot get_slot(size_t index) const;
    void destroy_buffers();
    void create_buffers();
};

struct MeshAllocation
{
    MeshSlot slot{};
    int32_t slot_index = -1;
    uint32_t indices_size = 0;
    //uint32_t gen = 0;
    VkDeviceSize vertex_slab_size = 0;
    VkDeviceSize index_slab_size = 0;
    MeshAllocator* allocator = nullptr;
};


#endif //MESH_ALLOCATOR_H
