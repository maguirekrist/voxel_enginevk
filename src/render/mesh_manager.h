#pragma once

#include <utils/blockingconcurrentqueue.h>
#include "mesh_payload.h"
#include "staging_buffer.h"

class MeshManager {
public:
    void init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue);

    void cleanup();

    void unload_garbage();
    void handle_transfers();
    std::shared_ptr<MeshRef> enqueue_upload(MeshPayload&& mesh);
    void enqueue_unload(MeshAllocation mesh_allocation);
private:
    VkDevice m_device{};
    VmaAllocator m_allocator{};
    QueueFamily m_transferQueue{};
    UploadContext m_uploadContext{};
    moodycamel::BlockingConcurrentQueue<MeshPayload> UploadQueue;
    moodycamel::BlockingConcurrentQueue<MeshAllocation> UnloadQueue;

    std::unique_ptr<StagingBuffer> m_stagingBuffer = nullptr;

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const;
    void unload_mesh(MeshAllocation mesh_allocation) const;
};
