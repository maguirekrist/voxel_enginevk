//
// Created by Maguire Krist on 8/22/25.
//

#ifndef STAGING_BUFFER_H
#define STAGING_BUFFER_H
#include "constants.h"
#include "mesh.h"
#include "mesh_allocator.h"

constexpr size_t approximate_vertex_buffer_size(const int viewDistance)
{
    return static_cast<size_t>(viewDistance) * TOTAL_BLOCKS_IN_CHUNK * 8 * sizeof(Vertex);
}

constexpr size_t approximate_index_buffer_size(const int viewDistance)
{
    return static_cast<size_t>(viewDistance) * TOTAL_BLOCKS_IN_CHUNK * 8 * sizeof(uint32_t);
}

constexpr size_t approximate_staging_buffer_size(const int viewDistance)
{
    return approximate_vertex_buffer_size(viewDistance) + approximate_index_buffer_size(viewDistance);
}

struct StagingBufferConfig
{
    size_t stagingBufferSize{approximate_staging_buffer_size(GameConfig::DEFAULT_VIEW_DISTANCE)};
    MeshAllocatorConfig meshAllocatorConfig{};
};


struct UploadHandle
{
    std::shared_ptr<Mesh> mesh;

    VkDeviceSize vertexOffset = 0;
    VkDeviceSize vertexSize = 0;
    VkDeviceSize indexOffset = 0;
    VkDeviceSize indexSize = 0;
};

class StagingBuffer {
public:
    explicit StagingBuffer(VmaAllocator vmaAllocator, StagingBufferConfig config = {});

    ~StagingBuffer();

    void begin_recording();
    void upload_mesh(std::shared_ptr<Mesh>&& mesh);
    [[nodiscard]] std::function<void(VkCommandBuffer cmd)> build_submission() const;
    void end_recording();
    void reconfigure(StagingBufferConfig config);
    [[nodiscard]] bool can_reconfigure() const noexcept;
    [[nodiscard]] const StagingBufferConfig& config() const noexcept;

    MeshAllocator m_meshAllocator;
private:
    void* m_write_head;
    VkDeviceSize m_write_offset = 0;

    bool m_recording = false;
    VkDeviceSize m_capacity;

    VmaAllocator m_allocator;
    AllocatedBuffer m_stagingBuffer{};
    std::vector<UploadHandle> m_uploadHandles{};
    StagingBufferConfig m_config{};

    uint64_t m_v_total_size = 0;
    uint64_t m_i_total_size = 0;
    uint32_t m_v_total_count = 0;
    uint32_t m_i_total_count = 0;

    void destroy_buffer();
    void create_buffer();
};



#endif //STAGING_BUFFER_H
