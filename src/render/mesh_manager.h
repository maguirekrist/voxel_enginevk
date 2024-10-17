#pragma once

#include <vk_types.h>
#include <vk_mesh.h>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>

class MeshManager {
public:
    void init(VkDevice device, VmaAllocator allocator, QueueFamily queue);

    void cleanup();

    //std::unordered_map<std::string, std::shared_ptr<Mesh>> _meshes;
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUploadQueue;
	moodycamel::ConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUnloadQueue;
    moodycamel::ConcurrentQueue<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>> > > _meshSwapQueue;
    
    void upload_mesh(Mesh& mesh);
	void unload_mesh(std::shared_ptr<Mesh>&& mesh);
private:
    VkDevice _device;
    VmaAllocator _allocator;

    QueueFamily _transferQueue;

    UploadContext _uploadContext;
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

    void handle_transfers();
	std::thread _transferThread;
};