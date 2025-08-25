#include "staging_buffer.h"

#include "vk_util.h"

StagingBuffer::StagingBuffer(VmaAllocator vmaAllocator): m_write_head(nullptr), m_capacity(APPROXIMATE_BUFFER_SIZE), m_allocator(vmaAllocator), m_meshAllocator(vmaAllocator)
{
    m_stagingBuffer = vkutil::create_buffer(m_allocator, m_capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VMA_MEMORY_USAGE_CPU_ONLY);
    m_uploadHandles.reserve(32);
}

StagingBuffer::~StagingBuffer()
{
    vmaDestroyBuffer(m_allocator, m_stagingBuffer._buffer, m_stagingBuffer._allocation);
}

void StagingBuffer::begin_recording()
{
    vmaMapMemory(m_allocator, m_stagingBuffer._allocation, &m_write_head);
    m_recording = true;
}

void StagingBuffer::upload_mesh(MeshPayload&& mesh)
{
    if (!m_recording) { throw std::runtime_error("StagingBuffer::upload_mesh: Not recording"); }
    uint32_t v_size = mesh._vertices.size() * sizeof(Vertex);
    uint32_t i_size = mesh._indices.size() * sizeof(uint32_t);
    const VkDeviceSize needed = m_write_offset + (v_size + i_size);

    if (needed > m_capacity)
    {
        throw std::runtime_error("StagingBuffer::upload_mesh: Buffer exhausted");
    }

    auto vertex_offset = m_write_offset;
    std::memcpy(m_write_head, mesh._vertices.data(), v_size);
    m_write_offset += v_size;
    m_write_head = static_cast<char*>(m_write_head) + v_size;
    auto index_offset = m_write_offset;
    std::memcpy(m_write_head, mesh._indices.data(), i_size);
    m_write_offset += i_size;
    m_write_head = static_cast<char*>(m_write_head) + i_size;

    auto allocation = m_meshAllocator.acquire();

    if (v_size > allocation.slab_size ||  i_size > allocation.slab_size)
    {
        throw std::runtime_error(std::format("StagingBuffer::upload_mesh: Slab size too small, slab size of {}, vs v size: {} and i size: {}", allocation.slab_size, v_size, i_size));
    }


    max_v_size = std::max(max_v_size, v_size);
    min_v_size = std::min(min_v_size, v_size);

    max_i_size = std::max(max_i_size, i_size);
    min_i_size = std::min(min_i_size, i_size);

    // m_v_total_count += 1;
    // m_v_total_size += v_size;
    // m_i_total_count += 1;
    // m_i_total_size += i_size;
    //
    // std::println("average v size: {}", m_v_total_size / m_v_total_count);
    // std::println("average i size: {}", m_i_total_size / m_i_total_count);

    //mesh->_allocation = allocation;
    allocation.indices_size = static_cast<uint32_t>(mesh._indices.size());
    mesh._mesh->allocation = allocation;

    m_uploadHandles.emplace_back(
        allocation,
        vertex_offset,
        v_size,
        index_offset,
        i_size);

}

std::function<void(VkCommandBuffer cmd)> StagingBuffer::build_submission() const
{
    return [this](VkCommandBuffer cmd)
    {
        for (const auto& handle : m_uploadHandles)
        {
            //USE Mesh Allocator
            VkBufferCopy vertex_copy;
            vertex_copy.dstOffset = handle.allocation.slot.vertex_offset;
            vertex_copy.srcOffset = handle.vertexOffset;
            vertex_copy.size = handle.vertexSize;
            vkCmdCopyBuffer(cmd, this->m_stagingBuffer._buffer, m_meshAllocator.m_vertexBuffer._buffer, 1, &vertex_copy);

            VkBufferCopy index_copy;
            index_copy.dstOffset = handle.allocation.slot.index_offset;
            index_copy.srcOffset = handle.indexOffset;
            index_copy.size = handle.indexSize;
            vkCmdCopyBuffer(cmd, this->m_stagingBuffer._buffer,m_meshAllocator.m_indexBuffer._buffer, 1, &index_copy);
            //TODO: Figure out how we should use is_active in handles?
            //handle.allocation.is_active = true;
        }
    };
}

void StagingBuffer::end_recording()
{
    m_recording = false;
    vmaUnmapMemory(m_allocator, m_stagingBuffer._allocation);
    m_uploadHandles.clear();
    m_write_head = nullptr;
    m_write_offset = 0;
}
