//
// Created by Maguire Krist on 8/23/25.
//

#include "mesh_allocator.h"
#include "vk_util.h"
#include "utils/logging_utils.h"

MeshAllocator::MeshAllocator(VmaAllocator allocator) : m_free_list(CAPACITY), m_allocator(allocator)
{
    m_vertexBuffer = vkutil::create_buffer(m_allocator, VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_indexBuffer = vkutil::create_buffer(m_allocator, INDEX_BUFFER_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    for (int32_t i = 0; i < CAPACITY; i++)
    {
        m_free_list[i] = i;
    }

    std::println("MeshAllocator - Vertex Buffer Size: {}, Index Buffer Size: {}", bytes_to_mb_string(VERTEX_BUFFER_SIZE), bytes_to_mb_string(INDEX_BUFFER_SIZE));
}

MeshAllocator::~MeshAllocator()
{
    vmaDestroyBuffer(m_allocator, m_vertexBuffer._buffer, m_vertexBuffer._allocation);
    vmaDestroyBuffer(m_allocator, m_indexBuffer._buffer, m_indexBuffer._allocation);
}

MeshAllocation MeshAllocator::acquire()
{
    if (m_free_list.empty())
    {
        throw std::runtime_error("no free slots");
    }

    auto free_index = m_free_list.back();
    m_free_list.pop_back();

    //latest free_index
    auto slot = get_slot(free_index);
    return MeshAllocation{ .slot = slot, .slot_index = free_index, .gen = 0, .slab_size = VERTEX_SLAB_SIZE, .allocator = this };
}

void MeshAllocator::free(MeshAllocation allocation)
{
    if (allocation.slot_index == -1)
    {
        return;
    }

    // mesh->_isActive.store(false, std::memory_order::release); //check this
    //auto slot_index = mesh->_allocation.slot_index;
    m_free_list.push_back(allocation.slot_index);
}

MeshSlot MeshAllocator::get_slot(const size_t index) const
{
    if (index >= CAPACITY)
    {
        throw std::runtime_error("index out of range");
    }

    //This returns "slot" which is really just index offsets into the respective global buffers.
    return MeshSlot { .vertex_offset = index * VERTEX_SLAB_SIZE , .index_offset = index * INDEX_SLAB_SIZE };
}
