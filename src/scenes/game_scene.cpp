#include "game_scene.h"
#include <vk_initializers.h>
#include <vk_mesh.h>

void GameScene::render(VkCommandBuffer cmd) {
	VkRenderPassBeginInfo rpOffscreenInfo = vkinit::render_pass_begin_info(_offscreenPass, _windowExtent, _offscreenFramebuffer, ClearFlags::Color | ClearFlags::Depth);

	update_fog_ubo();

	vkCmdBeginRenderPass(cmd, &rpOffscreenInfo, VK_SUBPASS_CONTENTS_INLINE);

	{
		ZoneScopedN("Draw Chunks & Objects");
		update_uniform_buffers();

		std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>>> meshSwap;
		while(_meshSwapQueue.try_dequeue(meshSwap))
		{	
			auto oldMesh = meshSwap.second->update(meshSwap.first);
			_meshManager.unload_mesh(std::move(oldMesh));
		}
		draw_objects(cmd, _game._chunkManager._renderedChunks, false);
		//draw_objects(cmd, _renderObjects.data(), _renderObjects.size());
	}
	
	//vkCmdExecuteCommands(cmd, get_current_frame()._secondaryCommandBuffers.size(), get_current_frame()._secondaryCommandBuffers.data());
	
	//draw_objects(cmd, _game._chunkManager._renderChunks.data(), _game._chunkManager._renderChunks.size());

	//finalize the render pass
	vkCmdEndRenderPass(cmd);

	//Transition is TRANSITION EVEN NEEDED?
	//vkutil::transition_image(cmd, _fullscreenImage._image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);


	run_compute(cmd, _computeMaterial, _computeDescriptorSets.data(), _computeDescriptorSets.size());



	//Do a swapchain renderpass
	VkRenderPassBeginInfo rpInfo = vkinit::render_pass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex], ClearFlags::Color);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	//Render off-screen image to the present renderpass
	draw_fullscreen(cmd, _materialManager.get_material("present"));

	draw_objects(cmd, _game._chunkManager._transparentObjects, true);

	vkCmdEndRenderPass(cmd);
}

void GameScene::run_compute(VkCommandBuffer cmd, const Material &computeMaterial, VkDescriptorSet *descriptorSets, size_t setCount)
{
    // Bind the compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeMaterial.pipeline);

    // Bind the descriptor set (which contains the image resources)
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computeMaterial.pipelineLayout,
        0, setCount, // First set, 1 descriptor set
        descriptorSets,
        0, nullptr // Dynamic offsets
    );

    // Dispatch the compute shader (assuming a full-screen image size)
    // Adjust the work group size based on your compute shader's `local_size_x` and `local_size_y`
    uint32_t workGroupSizeX = (_windowExtent.width + 15) / 16; // Assuming local size of 16 in shader
    uint32_t workGroupSizeY = (_windowExtent.height + 15) / 16;

    vkCmdDispatch(cmd, workGroupSizeX, workGroupSizeY, 1);

    // Insert a memory barrier to ensure the compute shader has finished executing
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; // Writes in compute shader
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // Reads and writes in subsequent shaders or pipeline stages

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Source stage
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, // Destination stages
        0,
        1, &memoryBarrier,
        0, nullptr,
        0, nullptr
    );
}

void GameScene::draw_fullscreen(VkCommandBuffer cmd, Material *presentMaterial)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, presentMaterial->pipeline);
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		presentMaterial->pipelineLayout,
		0, 1, // First set, 1 descriptor set
		&_sampledImageSet,
		0, nullptr // Dynamic offsets
	);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

void GameScene::draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial, bool isTransparent)
{
	if(object.mesh == nullptr) return;
	if(!object.mesh->get()->_isActive) return;

	//only bind the pipeline if it doesn't match with the already bound one
	if (object.material != lastMaterial) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
		lastMaterial = object.material;

		//BIND DESCRIPTORS
		vkCmdBindDescriptorSets(cmd, 
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		object.material->pipelineLayout,
		0,
		1,
		&get_current_frame()._globalDescriptor,
		0,
		nullptr);

		vkCmdBindDescriptorSets(cmd, 
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		object.material->pipelineLayout,
		1, 
		1, 
		&get_current_frame()._chunkDescriptor,
		0,
		nullptr);

		if(isTransparent)
		{
			vkCmdBindDescriptorSets(cmd, 
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			object.material->pipelineLayout,
			2, 
			1, 
			&_fogSet,
			0,
			nullptr);
		}
	}


	//EXAMPLE: PUSH CONTRAINTS
	ChunkPushConstants constants;
	constants.chunk_translate = object.xzPos;
	vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ChunkPushConstants), &constants);

	//only bind the mesh if it's a different one from last bind
	if(object.mesh->get().get() != lastMesh) {
					//bind the mesh vertex buffer with offset 0
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->get()->_vertexBuffer._buffer, &offset);

		vkCmdBindIndexBuffer(cmd, object.mesh->get()->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		lastMesh = object.mesh->get().get();
	}

	//we can now draw
	vkCmdDrawIndexed(cmd, object.mesh->get()->_indices.size(), 1, 0, 0, 0);
}

void GameScene::draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks, bool isTransparent)
{
	//Game is not finished with initial loading, do not render anything yet.
	if(_game._chunkManager._initLoad) return;

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;

	for(int i = 0; i < chunks.size(); i++)
	{
		auto& object = chunks[i];

		draw_object(cmd, object, lastMesh, lastMaterial, isTransparent);
	}

}
