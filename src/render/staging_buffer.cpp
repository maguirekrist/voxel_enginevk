#include "staging_buffer.h"

#include "vk_util.h"

StagingBuffer::StagingBuffer(VmaAllocator vmaAllocator, StagingBufferConfig config) :
    m_write_head(nullptr),
    m_capacity(static_cast<VkDeviceSize>(config.stagingBufferSize)),
    m_allocator(vmaAllocator),
    m_config(std::move(config))
{
    m_meshAllocator = create_mesh_allocator(vmaAllocator, m_config.meshAllocatorConfig);
    create_buffer();
    m_uploadHandles.reserve(32);
}

StagingBuffer::~StagingBuffer()
{
    destroy_buffer();
}

void StagingBuffer::begin_recording()
{
    vmaMapMemory(m_allocator, m_stagingBuffer._allocation, &m_write_head);
    m_recording = true;
}

void StagingBuffer::upload_mesh(std::shared_ptr<Mesh>&& mesh)
{
    if (!m_recording) { throw std::runtime_error("StagingBuffer::upload_mesh: Not recording"); }
    auto v_size = mesh->_vertices.size() * sizeof(Vertex);
    auto i_size = mesh->_indices.size() * sizeof(uint32_t);

    if (v_size == 0 || i_size == 0)
    {
        return;
    }

    const VkDeviceSize needed = m_write_offset + (v_size + i_size);

    if (needed > m_capacity)
    {
        throw std::runtime_error("StagingBuffer::upload_mesh: Buffer exhausted");
    }

    auto vertex_offset = m_write_offset;
    std::memcpy(m_write_head, mesh->_vertices.data(), v_size);
    m_write_offset += v_size;
    m_write_head = static_cast<char*>(m_write_head) + v_size;
    auto index_offset = m_write_offset;
    std::memcpy(m_write_head, mesh->_indices.data(), i_size);
    m_write_offset += i_size;
    m_write_head = static_cast<char*>(m_write_head) + i_size;

    auto allocation = m_meshAllocator->acquire(v_size, i_size);

    m_v_total_count += 1;
    m_v_total_size += v_size;
    m_i_total_count += 1;
    m_i_total_size += i_size;

    // std::println("average v size: {}", m_v_total_size / m_v_total_count);
    // std::println("average i size: {}", m_i_total_size / m_i_total_count);

    allocation.indicesSize = static_cast<uint32_t>(mesh->_indices.size());
    mesh->_allocation = allocation;

    //Clear out mesh memory
    mesh->_indices = std::vector<uint32_t>();
    mesh->_vertices = std::vector<Vertex>();

    m_uploadHandles.emplace_back(
        mesh,
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
            if (handle.vertexSize > 0)
            {
                VkBufferCopy vertex_copy;
                vertex_copy.dstOffset = handle.mesh->_allocation.vertexOffset;
                vertex_copy.srcOffset = handle.vertexOffset;
                vertex_copy.size = handle.vertexSize;
                const VkBuffer vertexBuffer = m_meshAllocator->vertex_buffer_handle();
                vkCmdCopyBuffer(cmd, this->m_stagingBuffer._buffer, vertexBuffer, 1, &vertex_copy);
            }

            if (handle.indexSize > 0)
            {
                VkBufferCopy index_copy;
                index_copy.dstOffset = handle.mesh->_allocation.indexOffset;
                index_copy.srcOffset = handle.indexOffset;
                index_copy.size = handle.indexSize;
                const VkBuffer indexBuffer = m_meshAllocator->index_buffer_handle();
                vkCmdCopyBuffer(cmd, this->m_stagingBuffer._buffer, indexBuffer, 1, &index_copy);
            }

            handle.mesh->_isActive.store(true, std::memory_order::relaxed);
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

void StagingBuffer::reconfigure(StagingBufferConfig config)
{
    if (!can_reconfigure())
    {
        throw std::runtime_error("StagingBuffer::reconfigure: staging buffer still has live allocations");
    }

    m_config = std::move(config);
    m_capacity = static_cast<VkDeviceSize>(m_config.stagingBufferSize);
    destroy_buffer();
    m_meshAllocator->reconfigure(m_config.meshAllocatorConfig);
    create_buffer();
}

bool StagingBuffer::can_reconfigure() const noexcept
{
    return !m_recording && m_uploadHandles.empty() && m_meshAllocator != nullptr && m_meshAllocator->can_reconfigure();
}

const StagingBufferConfig& StagingBuffer::config() const noexcept
{
    return m_config;
}

IMeshAllocator& StagingBuffer::mesh_allocator()
{
    return *m_meshAllocator;
}

const IMeshAllocator& StagingBuffer::mesh_allocator() const
{
    return *m_meshAllocator;
}

void StagingBuffer::destroy_buffer()
{
    if (m_stagingBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_stagingBuffer._buffer, m_stagingBuffer._allocation);
        m_stagingBuffer = {};
    }
}

void StagingBuffer::create_buffer()
{
    m_stagingBuffer = vkutil::create_buffer(
        m_allocator,
        m_capacity,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);
    std::println("Staging Buffer created with size: {} bytes", m_capacity);
}
