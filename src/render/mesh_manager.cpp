#include "mesh_manager.h"
#include <vk_initializers.h>
#include <tracy/Tracy.hpp>
#include <tiny_obj_loader.h>

#include "vk_engine.h"

void MeshManager::init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue)
{
	m_device = device;
	m_allocator = allocator;
	m_transferQueue = queue;

	VkFenceCreateInfo uploadCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(m_device, &uploadCreateInfo, nullptr, &m_uploadContext._uploadFence));

	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(m_transferQueue._queueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(m_device, &uploadCommandPoolInfo, nullptr, &m_uploadContext._commandPool));


	//allocate the default command buffer that we will use for the instant commands
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_uploadContext._commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_uploadContext._commandBuffer));

	m_stagingBuffer = std::make_unique<StagingBuffer>(m_allocator);

	//Start transfer thread
	//m_transferThread = std::thread(&MeshManager::handle_transfers, this);
}


void MeshManager::cleanup()
{
	unload_garbage();
    vkDestroyFence(m_device, m_uploadContext._uploadFence, nullptr);
    vkDestroyCommandPool(m_device, m_uploadContext._commandPool, nullptr);
}

void MeshManager::unload_garbage()
{
	ZoneScopedN("Handle Unload Meshes");
	MeshAllocation unloadMesh{};
	while(UnloadQueue.try_dequeue(unloadMesh))
	{
		unload_mesh(unloadMesh);
	}
}

// void MeshManager::upload_mesh(std::shared_ptr<Mesh>&& mesh) const
// {
// 	ZoneScopedN("upload_mesh");
// 	AllocatedBuffer vertexStagingBuffer = vkutil::create_buffer(m_allocator, mesh->_vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
// 	AllocatedBuffer indexStagingBuffer = vkutil::create_buffer(m_allocator, mesh->_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
//
// 	//copy vertex data
// 	void* vertexData;
// 	vmaMapMemory(m_allocator, vertexStagingBuffer._allocation, &vertexData);
// 	memcpy(vertexData, mesh->_vertices.data(), mesh->_vertices.size() * sizeof(Vertex));
// 	vmaUnmapMemory(m_allocator, vertexStagingBuffer._allocation);
//
// 	void* indexData;
// 	vmaMapMemory(m_allocator, indexStagingBuffer._allocation, &indexData);
// 	memcpy(indexData, mesh->_indices.data(), mesh->_indices.size() * sizeof(uint32_t));
// 	vmaUnmapMemory(m_allocator, indexStagingBuffer._allocation);
//
// 	mesh->_vertexBuffer = vkutil::create_buffer(m_allocator, mesh->_vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
// 	mesh->_indexBuffer = vkutil::create_buffer(m_allocator, mesh->_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
//
// 	immediate_submit([&mesh, indexStagingBuffer, vertexStagingBuffer](VkCommandBuffer cmd) {
// 		VkBufferCopy copy;
// 		copy.dstOffset = 0;
// 		copy.srcOffset = 0;
// 		copy.size = mesh->_vertices.size() * sizeof(Vertex);
// 		vkCmdCopyBuffer(cmd, vertexStagingBuffer._buffer, mesh->_vertexBuffer._buffer, 1, &copy);
//
// 		VkBufferCopy index_copy;
// 		index_copy.dstOffset = 0;
// 		index_copy.srcOffset = 0;
// 		index_copy.size = mesh->_indices.size() * sizeof(uint32_t);
// 		vkCmdCopyBuffer(cmd, indexStagingBuffer._buffer,mesh->_indexBuffer._buffer, 1, &index_copy);
// 	});
//
// 	vmaDestroyBuffer(m_allocator, vertexStagingBuffer._buffer, vertexStagingBuffer._allocation);
// 	vmaDestroyBuffer(m_allocator, indexStagingBuffer._buffer, indexStagingBuffer._allocation);
//
// 	mesh->_isActive.store(true, std::memory_order::release);
// 	//std::println("MeshManager::upload_mesh()");
// }

void MeshManager::unload_mesh(MeshAllocation mesh_allocation) const
{
	ZoneScopedN("unload_mesh()");
	m_stagingBuffer->m_meshAllocator.free(mesh_allocation);
	mesh_allocation.is_active = false;
}

//TODO: Re-implement tiny-obj uploads?

// std::shared_ptr<Mesh> MeshManager::queue_from_obj(const std::string &path)
// {
// 	tinyobj::attrib_t attrib;
// 	std::vector<tinyobj::shape_t> shapes;
// 	std::vector<tinyobj::material_t> materials;
// 	std::string warn, err;
//
// 	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
// 		throw std::runtime_error(warn + err);
// 	}
//
// 	Mesh mesh;
// 	// Reserve space for vertices and indices
// 	mesh._vertices.reserve(attrib.vertices.size() / 3);
// 	mesh._indices.reserve(shapes[0].mesh.indices.size());
//
// 	// Iterate over shapes and their indices to populate vertices and indices
// 	for (const auto& shape : shapes) {
// 		for (const auto& index : shape.mesh.indices) {
// 			Vertex vertex = {};
//
// 			// Populate vertex position
// 			vertex.position = {
// 				attrib.vertices[3 * index.vertex_index + 0],
// 				attrib.vertices[3 * index.vertex_index + 1],
// 				attrib.vertices[3 * index.vertex_index + 2]
// 			};
//
// 			// Populate vertex normal
// 			if (!attrib.normals.empty()) {
// 				vertex.normal = {
// 					attrib.normals[3 * index.normal_index + 0],
// 					attrib.normals[3 * index.normal_index + 1],
// 					attrib.normals[3 * index.normal_index + 2]
// 				};
// 			}
//
// 			// Populate vertex color (if available)
// 			if (!attrib.colors.empty()) {
// 				vertex.color = {
// 					attrib.colors[3 * index.vertex_index + 0],
// 					attrib.colors[3 * index.vertex_index + 1],
// 					attrib.colors[3 * index.vertex_index + 2]
// 				};
// 			} else {
// 				// Default color if not available
// 				vertex.color = {1.0f, 1.0f, 1.0f};
// 			}
//
// 			mesh._vertices.push_back(vertex);
// 			mesh._indices.push_back(mesh._vertices.size() - 1);
// 		}
// 	}
//
// 	auto meshPtr = std::make_shared<Mesh>(mesh);
// 	UploadQueue.enqueue(meshPtr);
//
// 	return meshPtr;
// }

void MeshManager::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function) const
{

	VkCommandBuffer cmd = m_uploadContext._commandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once before resetting, so we tell vulkan that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//execute the function
	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);

	//submit command buffer to the queue and execute it.
	// _uploadFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(m_transferQueue._queue, 1, &submit, m_uploadContext._uploadFence));

	vkWaitForFences(m_device, 1, &m_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(m_device, 1, &m_uploadContext._uploadFence);

	// reset the command buffers inside the command pool
	vkResetCommandPool(m_device, m_uploadContext._commandPool, 0);
}

void MeshManager::handle_transfers()
{
	ZoneScopedN("Handle Upload meshes");
	m_stagingBuffer->begin_recording();
	MeshPayload uploadMesh;
	while (UploadQueue.try_dequeue(uploadMesh))
	{
		m_stagingBuffer->upload_mesh(std::move(uploadMesh));
	}

	immediate_submit(m_stagingBuffer->build_submission());

	m_stagingBuffer->end_recording();
}

std::shared_ptr<MeshRef> MeshManager::enqueue_upload(MeshPayload&& mesh)
{
	auto ref = std::make_shared<MeshRef>();
	ref->allocator = &m_stagingBuffer->m_meshAllocator;
	mesh._mesh = ref;
	UploadQueue.enqueue(std::move(mesh));
	return ref;
}

void MeshManager::enqueue_unload(MeshAllocation mesh_allocation)
{
	UnloadQueue.enqueue(mesh_allocation);
}
