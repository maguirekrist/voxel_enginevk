#pragma once

#include <vk_mesh.h>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>

class MeshManager {
public:
    void init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue, std::shared_ptr<std::mutex> mutex);

    void cleanup();

    //std::unordered_map<std::string, std::shared_ptr<Mesh>> _meshes;
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUploadQueue;
	moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUnloadQueue;
    moodycamel::ConcurrentQueue<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>> > > _meshSwapQueue;
    
    void upload_mesh(Mesh& mesh);
	void unload_mesh(std::shared_ptr<Mesh>&& mesh);

    std::shared_ptr<Mesh> queue_from_obj(const std::string& path);
private:
    VkDevice _device;
    VmaAllocator _allocator;
    QueueFamily _transferQueue;
    UploadContext _uploadContext;
    std::thread _transferThread;
    std::shared_ptr<std::mutex> m_transferMutex;

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
    void handle_transfers();
};