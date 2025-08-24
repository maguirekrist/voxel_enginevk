#pragma once

#include <utils/blockingconcurrentqueue.h>

#include "mesh.h"
#include "staging_buffer.h"

class MeshManager {
public:
    void init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue);

    void cleanup();
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> UploadQueue;
	moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> UnloadQueue;

    void unload_garbage();
    void handle_transfers();

private:
    VkDevice m_device{};
    VmaAllocator m_allocator{};
    QueueFamily m_transferQueue{};
    UploadContext m_uploadContext{};

    std::unique_ptr<StagingBuffer> m_stagingBuffer = nullptr;

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const;

    // void upload_mesh(std::shared_ptr<Mesh>&& mesh) const;
    void unload_mesh(std::shared_ptr<Mesh>&& mesh) const;
};
