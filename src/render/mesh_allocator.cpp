//
// Created by Maguire Krist on 8/23/25.
//

#include "mesh_allocator.h"
#include "vk_util.h"
#include "mesh.h"

MeshAllocator::MeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config) :
    m_allocator(allocator),
    m_config(std::move(config))
{
    reconfigure(m_config);
}

MeshAllocator::~MeshAllocator()
{
    destroy_buffers();
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
    return MeshAllocation{
        .slot = slot,
        .slot_index = free_index,
        .vertex_slab_size = m_config.vertexSlabSize,
        .index_slab_size = m_config.indexSlabSize,
        .allocator = this
    };
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
    if (index >= m_config.slotCapacity)
    {
        throw std::runtime_error("index out of range");
    }

    //This returns "slot" which is really just index offsets into the respective global buffers.
    return MeshSlot {
        .vertex_offset = index * m_config.vertexSlabSize,
        .index_offset = index * m_config.indexSlabSize
    };
}

void MeshAllocator::reconfigure(MeshAllocatorConfig config)
{
    if (!can_reconfigure() && (m_vertexBuffer._buffer != VK_NULL_HANDLE || m_indexBuffer._buffer != VK_NULL_HANDLE))
    {
        throw std::runtime_error("MeshAllocator::reconfigure: allocator still has live mesh allocations");
    }

    m_config = std::move(config);
    if (m_config.slotCapacity == 0)
    {
        m_config.slotCapacity = 1;
    }

    destroy_buffers();
    create_buffers();

    m_free_list.resize(m_config.slotCapacity);
    for (size_t i = 0; i < m_config.slotCapacity; ++i)
    {
        m_free_list[i] = static_cast<int32_t>(i);
    }
}

bool MeshAllocator::can_reconfigure() const noexcept
{
    return m_free_list.size() == m_config.slotCapacity;
}

const MeshAllocatorConfig& MeshAllocator::config() const noexcept
{
    return m_config;
}

void MeshAllocator::destroy_buffers()
{
    if (m_vertexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_vertexBuffer._buffer, m_vertexBuffer._allocation);
        m_vertexBuffer = {};
    }

    if (m_indexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_indexBuffer._buffer, m_indexBuffer._allocation);
        m_indexBuffer = {};
    }
}

void MeshAllocator::create_buffers()
{
    const VkDeviceSize vertexBufferSize = m_config.vertexSlabSize * m_config.slotCapacity;
    const VkDeviceSize indexBufferSize = m_config.indexSlabSize * m_config.slotCapacity;

    m_vertexBuffer = vkutil::create_buffer(
        m_allocator,
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_indexBuffer = vkutil::create_buffer(
        m_allocator,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::println("Mesh Vertex buffer created with size of: {} bytes", vertexBufferSize);
    std::println("Mesh Index buffer created with size of {} bytes", indexBufferSize);
}
