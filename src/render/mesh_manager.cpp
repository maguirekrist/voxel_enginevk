#include "mesh_manager.h"
#include <vk_initializers.h>
#include <tracy/Tracy.hpp>
#include <vk_util.h>
#include <tiny_obj_loader.h>

void MeshManager::init(VkDevice device, VmaAllocator allocator, QueueFamily queue)
{
    _device = device;
    _allocator = allocator;
    _transferQueue = queue;

    VkFenceCreateInfo uploadCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(_device, &uploadCreateInfo, nullptr, &_uploadContext._uploadFence));

    VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_transferQueue._queueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));


	//allocate the default command buffer that we will use for the instant commands
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext._commandBuffer));

    //Start transfer thread
    _transferThread = std::thread(&MeshManager::handle_transfers, this);
}

void MeshManager::cleanup()
{
    vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
    vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
}

void MeshManager::upload_mesh(Mesh& mesh)
{
	AllocatedBuffer vertexStagingBuffer = vkutil::create_buffer(_allocator, mesh._vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	AllocatedBuffer indexStagingBuffer = vkutil::create_buffer(_allocator, mesh._indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	//copy vertex data
	void* vertexData;
	vmaMapMemory(_allocator, vertexStagingBuffer._allocation, &vertexData);
	memcpy(vertexData, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, vertexStagingBuffer._allocation);

	void* indexData;
	vmaMapMemory(_allocator, indexStagingBuffer._allocation, &indexData);
	memcpy(indexData, mesh._indices.data(), mesh._indices.size() * sizeof(uint32_t));
	vmaUnmapMemory(_allocator, indexStagingBuffer._allocation);	

	mesh._vertexBuffer = vkutil::create_buffer(_allocator, mesh._vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	mesh._indexBuffer = vkutil::create_buffer(_allocator, mesh._indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = mesh._vertices.size() * sizeof(Vertex);
		vkCmdCopyBuffer(cmd, vertexStagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
	});

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = mesh._indices.size() * sizeof(uint32_t);
		vkCmdCopyBuffer(cmd, indexStagingBuffer._buffer,mesh._indexBuffer._buffer, 1, &copy);
	});

	vmaDestroyBuffer(_allocator, vertexStagingBuffer._buffer, vertexStagingBuffer._allocation);
	vmaDestroyBuffer(_allocator, indexStagingBuffer._buffer, indexStagingBuffer._allocation);

	mesh._isActive = true;
}

void MeshManager::unload_mesh(std::shared_ptr<Mesh>&& mesh)
{
	if(mesh.use_count() == 1)
	{
		vmaDestroyBuffer(_allocator, mesh->_vertexBuffer._buffer, mesh->_vertexBuffer._allocation);
		vmaDestroyBuffer(_allocator, mesh->_indexBuffer._buffer, mesh->_indexBuffer._allocation);
	} else {
		throw std::runtime_error("Attempted to unload a mesh that is referenced elsewhere.");
	}
}

std::shared_ptr<Mesh> MeshManager::queue_from_obj(const std::string &path)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
		throw std::runtime_error(warn + err);
	}

	Mesh mesh;
	// Reserve space for vertices and indices
	mesh._vertices.reserve(attrib.vertices.size() / 3);
	mesh._indices.reserve(shapes[0].mesh.indices.size());

	// Iterate over shapes and their indices to populate vertices and indices
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex = {};

			// Populate vertex position
			vertex.position = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			// Populate vertex normal
			if (!attrib.normals.empty()) {
				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2]
				};
			}

			// Populate vertex color (if available)
			if (!attrib.colors.empty()) {
				vertex.color = {
					attrib.colors[3 * index.vertex_index + 0],
					attrib.colors[3 * index.vertex_index + 1],
					attrib.colors[3 * index.vertex_index + 2]
				};
			} else {
				// Default color if not available
				vertex.color = {1.0f, 1.0f, 1.0f};
			}

			mesh._vertices.push_back(vertex);
			mesh._indices.push_back(mesh._vertices.size() - 1);
		}
	}

	auto meshPtr = std::make_shared<Mesh>(mesh);
	_mainMeshUploadQueue.enqueue(meshPtr);

	return meshPtr;
}

void MeshManager::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function)
{

	VkCommandBuffer cmd = _uploadContext._commandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once before resetting, so we tell vulkan that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//execute the function
	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);


	//submit command buffer to the queue and execute it.
	// _uploadFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_transferQueue._queue, 1, &submit, _uploadContext._uploadFence));

	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	// reset the command buffers inside the command pool
	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}

void MeshManager::handle_transfers()
{
	while(true)
	{	
		//Handle uploads and unloads
		{
			ZoneScopedN("Handle Unload and Upload meshes");

			std::shared_ptr<Mesh> unloadMesh;
			while(_mainMeshUnloadQueue.try_dequeue(unloadMesh))
			{
				unload_mesh(std::move(unloadMesh));
			}

			std::shared_ptr<Mesh> uploadMesh;
			_mainMeshUploadQueue.wait_dequeue(uploadMesh);
			upload_mesh(*uploadMesh);
		}
	}
}