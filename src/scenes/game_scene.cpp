#include "game_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <vk_mesh.h>

void GameScene::render(VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
	VkRenderPassBeginInfo rpOffscreenInfo = vkinit::render_pass_begin_info(VulkanEngine::instance()._offscreenPass, VulkanEngine::instance()._windowExtent, VulkanEngine::instance()._offscreenFramebuffer, ClearFlags::Color | ClearFlags::Depth);

	update_fog_ubo();

	vkCmdBeginRenderPass(cmd, &rpOffscreenInfo, VK_SUBPASS_CONTENTS_INLINE);

	{
		ZoneScopedN("Draw Chunks & Objects");
		update_uniform_buffer();

		std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>>> meshSwap;
		while(VulkanEngine::instance()._meshManager._meshSwapQueue.try_dequeue(meshSwap))
		{	
			auto oldMesh = meshSwap.second->update(meshSwap.first);
			VulkanEngine::instance()._meshManager.unload_mesh(std::move(oldMesh));
		}
		draw_objects(cmd, _game._chunkManager._renderedChunks);
		//draw_objects(cmd, _renderObjects.data(), _renderObjects.size());
	}
	
	//vkCmdExecuteCommands(cmd, get_current_frame()._secondaryCommandBuffers.size(), get_current_frame()._secondaryCommandBuffers.data());
	
	//draw_objects(cmd, _game._chunkManager._renderChunks.data(), _game._chunkManager._renderChunks.size());

	//finalize the render pass
	vkCmdEndRenderPass(cmd);

	//Transition is TRANSITION EVEN NEEDED?
	//vkutil::transition_image(cmd, _fullscreenImage._image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);


	run_compute(cmd, *VulkanEngine::instance()._materialManager.get_material("compute"));



	//Do a swapchain renderpass
	VkRenderPassBeginInfo rpInfo = vkinit::render_pass_begin_info(VulkanEngine::instance()._renderPass, VulkanEngine::instance()._windowExtent, VulkanEngine::instance()._framebuffers[swapchainImageIndex], ClearFlags::Color);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	//Render off-screen image to the present renderpass
	draw_fullscreen(cmd, VulkanEngine::instance()._materialManager.get_material("present"));

	draw_objects(cmd, _game._chunkManager._transparentObjects);

	vkCmdEndRenderPass(cmd);
}

void GameScene::update(float deltaTime)
{
	_game._player._moveSpeed = DEFAULT_MOVE_SPEED * deltaTime;
	_camera.update_view(_game._player._position, _game._player._front, _game._player._up);
    _game.update();
}

void GameScene::cleanup()
{
    _game.cleanup();
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
		case SDL_MOUSEBUTTONDOWN:
			_targetBlock = _camera.get_target_block(_game._world, _game._player);
			if(_targetBlock.has_value())
			{
				auto block = _targetBlock.value()._block;
				auto chunk = _targetBlock.value()._chunk;
				glm::vec3 worldBlockPos = _targetBlock.value()._worldPos;
				//build_target_block_view(worldBlockPos);
				//fmt::println("Current target block: Block(x{}, y{}, z{}, light: {}), at distance: {}", block->_position.x, block->_position.y, block->_position.z, block->_sunlight, _targetBlock.value()._distance);
				//fmt::println("Current chunk: Chunk(x: {}, y: {})", chunk->_position.x, chunk->_position.y);
			}
			break;
		case SDL_MOUSEMOTION:
			_game._player.handle_mouse_move(event.motion.xrel, event.motion.yrel);
			break;
	}
}

void GameScene::handle_keystate(const Uint8* state)
{
	if (state[SDL_SCANCODE_W])
	{
		_game._player.move_forward();
	}

	if (state[SDL_SCANCODE_S])
	{
		_game._player.move_backward();
	}

	if (state[SDL_SCANCODE_A])
	{
		_game._player.move_left();
	}

	if (state[SDL_SCANCODE_D])
	{
		_game._player.move_right();
	}
}

void GameScene::run_compute(VkCommandBuffer cmd, const Material &computeMaterial)
{
    // Bind the compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeMaterial.pipeline);

    // Bind the descriptor set (which contains the image resources)
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computeMaterial.pipelineLayout,
        0, computeMaterial.setCount,
        computeMaterial.descriptorSets,
        0, nullptr
    );

    // Dispatch the compute shader (assuming a full-screen image size)
    // Adjust the work group size based on your compute shader's `local_size_x` and `local_size_y`
    uint32_t workGroupSizeX = (VulkanEngine::instance()._windowExtent.width + 15) / 16; // Assuming local size of 16 in shader
    uint32_t workGroupSizeY = (VulkanEngine::instance()._windowExtent.height + 15) / 16;

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
		0, presentMaterial->setCount,
		presentMaterial->descriptorSets,
		0, nullptr
	);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

void GameScene::draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial)
{
	if(object.mesh == nullptr) return;
	if(!object.mesh->get()->_isActive) return;

	//only bind the pipeline if it doesn't match with the already bound one
	if (object.material != lastMaterial) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
		lastMaterial = object.material;

		vkCmdBindDescriptorSets(cmd, 
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		object.material->pipelineLayout,
		0, 
		object.material->setCount,
		object.material->descriptorSets,
		0,
		nullptr);
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

void GameScene::draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks)
{
	//Game is not finished with initial loading, do not render anything yet.
	if(_game._chunkManager._initLoad) return;

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;

	for(int i = 0; i < chunks.size(); i++)
	{
		auto& object = chunks[i];

		draw_object(cmd, object, lastMesh, lastMaterial);
	}

}

void GameScene::update_fog_ubo()
{
	FogUBO fogUBO;
	fogUBO.fogColor = static_cast<glm::vec3>(Colors::skyblueHigh);
	fogUBO.fogEndColor = static_cast<glm::vec3>(Colors::skyblueLow);

	fogUBO.fogCenter = _game._player._position;
	fogUBO.fogRadius = (CHUNK_SIZE * DEFAULT_VIEW_DISTANCE) - 60.0f;
	fogUBO.screenSize = glm::ivec2(VulkanEngine::instance()._windowExtent.width, VulkanEngine::instance()._windowExtent.height);
	fogUBO.invViewProject = glm::inverse(_camera._projection * _camera._view);

	void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, _fogUboBuffer._allocation, &data);
	memcpy(data, &fogUBO, sizeof(FogUBO));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, _fogUboBuffer._allocation);
}

void GameScene::update_uniform_buffer()
{
	CameraUBO cameraUBO;
	cameraUBO.projection = _camera._projection;
	cameraUBO.view = _camera._view;
	cameraUBO.viewproject = _camera._projection * _camera._view; 

	void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, VulkanEngine::instance().get_current_frame()._cameraBuffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, VulkanEngine::instance().get_current_frame()._cameraBuffer._allocation);
}

void GameScene::update_chunk_buffer()
{
	if(_game._chunkManager._initLoad == true) return;

	void* objectData;
	vmaMapMemory(VulkanEngine::instance()._allocator, VulkanEngine::instance().get_current_frame()._chunkBuffer._allocation, &objectData);

	ChunkBufferObject* objectSSBO = (ChunkBufferObject*)objectData;

	for (int i = 0; i < _game._chunkManager._renderedChunks.size(); i++)
	{
		auto& object = _game._chunkManager._renderedChunks[i];
		objectSSBO[i].chunkPosition = object.xzPos;
	}

	vmaUnmapMemory(VulkanEngine::instance()._allocator, VulkanEngine::instance().get_current_frame()._chunkBuffer._allocation);
}