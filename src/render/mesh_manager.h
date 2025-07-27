#pragma once

#include <vk_mesh.h>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>

class MeshManager {
public:


    void init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue, std::shared_ptr<std::mutex> mutex);

    void cleanup() const;

    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> UploadQueue;
	moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> UnloadQueue;

    //moodycamel::ConcurrentQueue<std::pair<std::unique_ptr<Mesh>, std::unique_ptr<SharedResource<Mesh>> > > SwapQueue;
    //std::shared_ptr<Mesh> queue_from_obj(const std::string& path);
private:
    VkDevice m_device{};
    VmaAllocator m_allocator{};
    QueueFamily m_transferQueue{};
    UploadContext m_uploadContext{};
    std::thread m_transferThread;
    std::shared_ptr<std::mutex> m_transferMutex;

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const;
    void handle_transfers();

    void upload_mesh(const std::shared_ptr<Mesh>& mesh) const;
    void unload_mesh(const std::shared_ptr<Mesh>& mesh) const;
};