//
// Created by Maguire Krist on 8/22/25.
//

#ifndef STAGING_BUFFER_H
#define STAGING_BUFFER_H
#include "constants.h"
#include "mesh_payload.h"
#include "mesh_allocator.h"

constexpr size_t APPROXIMATE_VERTEX_BUFFER_SIZE = GameConfig::DEFAULT_VIEW_DISTANCE * TOTAL_BLOCKS_IN_CHUNK * 8 * sizeof(Vertex);
constexpr size_t APPROXIMATE_INDEX_BUFFER_SIZE = GameConfig::DEFAULT_VIEW_DISTANCE * TOTAL_BLOCKS_IN_CHUNK * 8 * sizeof(uint32_t);
constexpr size_t APPROXIMATE_BUFFER_SIZE = APPROXIMATE_INDEX_BUFFER_SIZE + APPROXIMATE_VERTEX_BUFFER_SIZE;


struct UploadHandle
{
    //MeshPayload payload;
    MeshAllocation allocation;
    VkDeviceSize vertexOffset = 0;
    VkDeviceSize vertexSize = 0;
    VkDeviceSize indexOffset = 0;
    VkDeviceSize indexSize = 0;
};

class StagingBuffer {
public:
    explicit StagingBuffer(VmaAllocator vmaAllocator);

    ~StagingBuffer();

    void begin_recording();
    void upload_mesh(MeshPayload&& mesh);
    [[nodiscard]] std::function<void(VkCommandBuffer cmd)> build_submission() const;
    void end_recording();

    MeshAllocator m_meshAllocator;
private:
    void* m_write_head;
    VkDeviceSize m_write_offset = 0;

    bool m_recording = false;
    VkDeviceSize m_capacity;

    VmaAllocator m_allocator;
    AllocatedBuffer m_stagingBuffer{};
    std::vector<UploadHandle> m_uploadHandles{};

    uint32_t min_v_size = std::numeric_limits<uint32_t>::min();
    uint32_t min_i_size = std::numeric_limits<uint32_t>::min();
    uint32_t max_v_size = std::numeric_limits<uint32_t>::max();
    uint32_t max_i_size = std::numeric_limits<uint32_t>::max();

    // uint64_t m_v_total_size = 0;
    // uint64_t m_i_total_size = 0;
    // uint32_t m_v_total_count = 0;
    // uint32_t m_i_total_count = 0;
};



#endif //STAGING_BUFFER_H
