
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "constants.h"
#include "tracy/Tracy.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vk_initializers.h>
#include <sdl_utils.h>
#include <vulkan/vulkan_core.h>

#include <cube_engine.h>

#include "VkBootstrap.h"
#include "vk_mesh.h"
#include "vk_types.h"
#include "vk_pipeline_builder.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

void VulkanEngine::calculate_fps()
{
	std::chrono::duration<float> elapsedTime = Clock::now() - _lastFpsTime;

	if(elapsedTime.count() >= 1.0f)
	{
		_fps = _frameNumber / elapsedTime.count();
		fmt::println("FPS: {}", _fps);

		_frameNumber = 0;
		_lastFpsTime = Clock::now();
	}
}

void VulkanEngine::handle_transfers()
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

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	init_vulkan();

	init_swapchain();

	init_offscreen_images();

	init_commands();

	init_default_renderpass();

	init_offscreen_renderpass();

	init_framebuffers();

	init_offscreen_framebuffers();

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	_transferThread = std::thread(&VulkanEngine::handle_transfers, this);

	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized) {

		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

		_mainDeletionQueue.flush();

		vkDestroyDevice(_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::handle_input()
{	
	SDL_Event e;
	const Uint8* state = SDL_GetKeyboardState(NULL);
	//Handle events on queue
	while (SDL_PollEvent(&e) != 0)
	{
		switch(e.type) {
			case SDL_KEYDOWN:
				switch(e.key.keysym.sym)
				{
					case SDLK_ESCAPE:
						bFocused = false;
						SDL_SetWindowGrab(_window, SDL_FALSE);
						SDL_SetRelativeMouseMode(SDL_FALSE);
						SDL_ShowCursor(SDL_TRUE);
						break;
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(bFocused == false)
				{
					fmt::println("Mouse Button down");
					bFocused = true;
					SDL_SetWindowGrab(_window, SDL_TRUE);
					SDL_SetRelativeMouseMode(SDL_TRUE);
					SDL_ShowCursor(SDL_FALSE);
				} else {
					_targetBlock = _camera.get_target_block(_game._world, _game._player);
					if(_targetBlock.has_value())
					{
						auto block = _targetBlock.value()._block;
						auto chunk = _targetBlock.value()._chunk;
						glm::vec3 worldBlockPos = _targetBlock.value()._worldPos;
						build_target_block_view(worldBlockPos);
						//fmt::println("Current target block: Block(x{}, y{}, z{}, light: {}), at distance: {}", block->_position.x, block->_position.y, block->_position.z, block->_sunlight, _targetBlock.value()._distance);
						fmt::println("Current chunk: Chunk(x: {}, y: {})", chunk->_position.x, chunk->_position.y);
					}
				}
				break;
			case SDL_MOUSEMOTION:
				if (bFocused == true)
				{
					_game._player.handle_mouse_move(e.motion.xrel, e.motion.yrel);
				}
				break;
			case SDL_QUIT:
				bQuit = true;
				break;
		}
	}

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

void VulkanEngine::update_uniform_buffers()
{
	CameraUBO cameraUBO;
	cameraUBO.projection = _camera._projection;
	cameraUBO.view = _camera._view;
	cameraUBO.viewproject = _camera._projection * _camera._view; 

	void* data;
	vmaMapMemory(_allocator, get_current_frame()._cameraBuffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(_allocator, get_current_frame()._cameraBuffer._allocation);
}

void VulkanEngine::update_chunk_buffer()
{
	if(_game._chunkManager._initLoad == true) return;

	void* objectData;
	vmaMapMemory(_allocator, get_current_frame()._chunkBuffer._allocation, &objectData);

	ChunkBufferObject* objectSSBO = (ChunkBufferObject*)objectData;

	for (int i = 0; i < _game._chunkManager._renderedChunks.size(); i++)
	{
		auto& object = _game._chunkManager._renderedChunks[i];
		objectSSBO[i].chunkPosition = object.xzPos;
	}

	vmaUnmapMemory(_allocator, get_current_frame()._chunkBuffer._allocation);
}

void VulkanEngine::run_compute(VkCommandBuffer cmd, Material computeMaterial, VkDescriptorSet descriptorSet)
{
	// Bind the compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeMaterial.pipeline);

    // Bind the descriptor set (which contains the image resources)
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computeMaterial.pipelineLayout,
        0, 1, // First set, 1 descriptor set
        &descriptorSet,
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

void VulkanEngine::advance_frame()
{
	{
		ZoneScopedN("Wait for GPU");
		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
		VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
	}

	//request image from the swapchain, one second timeout
	uint32_t swapchainImageIndex;
	{
		ZoneScopedN("Request Image");
		VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));
	}

	{
		ZoneScopedN("Reset buffer");
		//now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
		VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));
	}
}

VkCommandBuffer VulkanEngine::begin_recording()
{
	//naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	return cmd;
}

void VulkanEngine::draw()
{
	ZoneScopedN("RenderFrame");
	
	advance_frame();

	VkCommandBuffer cmd = begin_recording();

	//make a clear-color from frame number. This will flash with a 120*pi frame period.
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;

	VkClearValue clearValues[] = { clearValue, depthClear };
	//start the offscreen renderpass.
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo rpOffscreenInfo = {};
	rpOffscreenInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpOffscreenInfo.pNext = nullptr;
	rpOffscreenInfo.renderPass = _offscreenPass;
	rpOffscreenInfo.renderArea.offset.x = 0;
	rpOffscreenInfo.renderArea.offset.y = 0;
	rpOffscreenInfo.renderArea.extent = _windowExtent;
	rpOffscreenInfo.framebuffer = _offscreenFramebuffer;
	rpOffscreenInfo.clearValueCount = 2;
	rpOffscreenInfo.pClearValues = &clearValues[0];

	vkCmdBeginRenderPass(cmd, &rpOffscreenInfo, VK_SUBPASS_CONTENTS_INLINE);

	{
		ZoneScopedN("Draw Chunks & Objects");
		update_uniform_buffers();

		std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>>> meshSwap;
		while(_meshSwapQueue.try_dequeue(meshSwap))
		{	
			auto oldMesh = meshSwap.second->update(meshSwap.first);
			unload_mesh(std::move(oldMesh));
		}

		//update_chunk_buffer();
		draw_chunks(cmd);
		//draw_objects(cmd, _renderObjects.data(), _renderObjects.size());
	}
	
	//TODO: Implement secondary command buffers, use this call to record secondary command buffers to the primary command buffer.
	//vkCmdExecuteCommands(cmd, get_current_frame()._secondaryCommandBuffers.size(), get_current_frame()._secondaryCommandBuffers.data());
	
	//draw_objects(cmd, _game._chunkManager._renderChunks.data(), _game._chunkManager._renderChunks.size());

	//finalize the render pass
	vkCmdEndRenderPass(cmd);

	//Transition is TRANSITION EVEN NEEDED?
	//vkutil::transition_image(cmd, _fullscreenImage._image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkDescriptorSet colorImageSet;
	VkDescriptorImageInfo colorImageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = _fullscreenImageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_image(0, &colorImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(colorImageSet);

	VkDescriptorSet depthImageSet;
	VkDescriptorImageInfo depthImageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = _depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_image(1, &depthImageInfo, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(depthImageSet);

	VkDescriptorSet fogSet;
	VkDescriptorBufferInfo fogBufferInfo{
		.
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_buffer(2, )
		.build(fogSet);

	run_compute(cmd, _computeMaterial, );

	//Do a swapchain renderpass
	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;
	rpInfo.renderPass = _renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = _windowExtent;
	rpInfo.framebuffer = _framebuffers[swapchainImageIndex];
	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = &clearValues[0];

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	//Render off-screen image to the present renderpass



	vkCmdEndRenderPass(cmd);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));
	

	submit_queue_present(cmd, swapchainImageIndex);

	//increase the number of frames drawn
	_frameNumber++;
}

void VulkanEngine::submit_queue_present(VkCommandBuffer pCmd, uint32_t swapchainImageIndex)
{
	ZoneScopedN("Submit & Present");
		//prepare the submission to the queue.
	//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
	//we will signal the _renderSemaphore, to signal that rendering has finished

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &pCmd;

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

	// this will put the image we just rendered into the visible window.
	// we want to wait on the _renderSemaphore for that,
	// as it's necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
}

void VulkanEngine::run()
{

	//main loop
	while (!bQuit)
	{
		TimePoint now = std::chrono::steady_clock::now();
		std::chrono::duration<float> test = now - _lastFrameTime;
		_deltaTime = test.count();
		_lastFrameTime = now;
		_game._player._moveSpeed = DEFAULT_MOVE_SPEED * _deltaTime;


		handle_input();

		_camera.update_view(_game._player._position, _game._player._front, _game._player._up);

		_game.update();

		draw();
	}
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Example Vulkan Application")
	.request_validation_layers(bUseValidationLayers)
	.use_default_debug_messenger()
	.require_api_version(1, 1, 0)
	.build();

	vkb::Instance vkb_inst = inst_ret.value();

	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	// //vulkan 1.3 features
	// VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	// features.dynamicRendering = true;
	// features.synchronization2 = true;

	//vulkan 1.2 features
	// VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	// features12.bufferDeviceAddress = true;
	// features12.descriptorIndexing = true;


	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		// .set_required_features_13(features)
		// .set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();


	//create the final vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_gpuProperties = vkbDevice.physical_device.properties;

	std::cout << "The GPU has a minimum buffer alignment of " << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;

	// Get the VkDevice handle used in the rest of a vulkan application
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	auto transferQueue = vkbDevice.get_queue(vkb::QueueType::transfer);
	auto transferQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::transfer);

	if(transferQueue.has_value() && transferQueueFamily.has_value())
	{
		_transferQueue = transferQueue.value();
		_transferQueueFamily = transferQueueFamily.value();
	} else {
		_transferQueue = _graphicsQueue;
		_transferQueueFamily = _graphicsQueueFamily;
	}

	VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

	_descriptorAllocator.init(_device);
	_descriptorLayoutCache.init(_device);
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{_chosenGPU,_device,_surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	});

}

void VulkanEngine::init_offscreen_images()
{
	VkExtent3D windowImageExtent = {
        _windowExtent.width,
        _windowExtent.height,
        1
    };

	//CREATE COLOR IMAGE

	_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

	//SET-UP OFF-SCREEN IMAGE
	VkImageCreateInfo color_info = vkinit::image_create_info(_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, windowImageExtent);
	VmaAllocationCreateInfo cimg_allocinfo = {};
	cimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	cimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vmaCreateImage(_allocator, &color_info, &cimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo cview_info = vkinit::imageview_create_info(_colorFormat, _fullscreenImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &cview_info, nullptr, &_fullscreenImageView));

	_mainDeletionQueue.push_function([&]() {
		vkDestroyImageView(_device, _fullscreenImageView, nullptr);
		vmaDestroyImage(_allocator, _fullscreenImage._image, _fullscreenImage._allocation);
	});

	//CREATE DEPTH IMAGE

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	//the depth image will be an image with the format we selected and Depth Attachment usage flag
	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, windowImageExtent);

	//for the depth image, we want to allocate it from GPU local memory
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImageView));

	_mainDeletionQueue.push_function([&]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
	});

}

// void VulkanEngine::init_compute_descriptors()
// {
// 	VkDescriptorSetLayoutBinding colorImageBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0);
// 	VkDescriptorSetLayoutBinding depthImageBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 1);

	

// 	VkDescriptorSetLayoutBinding fogParamBindings[3] = { /* Bind fogColor, fogStart, fogEnd */ };


// }

void VulkanEngine::init_descriptors()
{
	//create a descriptor pool that will hold 10 uniform buffers
	std::vector<VkDescriptorPoolSize> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = (uint32_t)sizes.size();
	pool_info.pPoolSizes = sizes.data();

	if (vkCreateDescriptorPool(_device, &pool_info, nullptr, &_dPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor pool!");
	}

	//information about the binding.
	VkDescriptorSetLayoutBinding camBufferBinding = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0);

	VkDescriptorSetLayoutBinding bindings[] = { camBufferBinding };

	VkDescriptorSetLayoutCreateInfo setinfo = {};
	setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setinfo.pNext = nullptr;
	setinfo.bindingCount = 1;
	setinfo.flags = 0;
	setinfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(_device, &setinfo, nullptr, &_uboSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor set layout!");
	}

	VkDescriptorSetLayoutBinding objectBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set2info = {};
	set2info.bindingCount = 1;
	set2info.flags = 0;
	set2info.pNext = nullptr;
	set2info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2info.pBindings = &objectBind;

	vkCreateDescriptorSetLayout(_device, &set2info, nullptr, &_chunkSetLayout);

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyDescriptorSetLayout(_device, _uboSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _chunkSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _dPool, nullptr);
	});

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i]._cameraBuffer = create_buffer(sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i]._chunkBuffer = create_buffer(sizeof(ChunkBufferObject) * MAXIMUM_CHUNKS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		//allocate one descriptor set for each frame
		VkDescriptorSetAllocateInfo camAllocInfo ={};
		camAllocInfo.pNext = nullptr;
		camAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		camAllocInfo.descriptorPool = _dPool;
		camAllocInfo.descriptorSetCount = 1;
		camAllocInfo.pSetLayouts = &_uboSetLayout;

		if (vkAllocateDescriptorSets(_device, &camAllocInfo, &_frames[i]._globalDescriptor) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set!");
		}

		//allocate one descriptor set for each frame
		VkDescriptorSetAllocateInfo bufferAllocInfo ={};
		bufferAllocInfo.pNext = nullptr;
		bufferAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		bufferAllocInfo.descriptorPool = _dPool;
		bufferAllocInfo.descriptorSetCount = 1;
		bufferAllocInfo.pSetLayouts = &_chunkSetLayout;

		if (vkAllocateDescriptorSets(_device, &bufferAllocInfo, &_frames[i]._chunkDescriptor) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set!");
		}

		//information about the buffer we want to point at in the descriptor
		VkDescriptorBufferInfo cameraBufferInfo;
		cameraBufferInfo.buffer = _frames[i]._cameraBuffer._buffer;
		cameraBufferInfo.offset = 0;
		cameraBufferInfo.range = sizeof(CameraUBO);

		VkDescriptorBufferInfo chunkBufferInfo;
		chunkBufferInfo.buffer = _frames[i]._chunkBuffer._buffer;
		chunkBufferInfo.offset = 0;
		chunkBufferInfo.range = sizeof(ChunkBufferObject) * MAXIMUM_CHUNKS;


		VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _frames[i]._globalDescriptor,&cameraBufferInfo,0);

		VkWriteDescriptorSet chunkWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i]._chunkDescriptor, &chunkBufferInfo, 0);

		VkWriteDescriptorSet setWrites[] = { cameraWrite, chunkWrite };

		vkUpdateDescriptorSets(_device, 2, setWrites, 0, nullptr);
	}

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		// add storage buffers to deletion queues
		_mainDeletionQueue.push_function([&]() {
			vmaDestroyBuffer(_allocator, _frames[i]._cameraBuffer._buffer, _frames[i]._cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i]._chunkBuffer._buffer, _frames[i]._chunkBuffer._allocation);

			//other code ....
		});
	};
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeletionQueue.push_function([&]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
		});
	}

	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_transferQueueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([&]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
	});

	//allocate the default command buffer that we will use for the instant commands
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext._commandBuffer));

}

void VulkanEngine::init_offscreen_renderpass()
{
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _colorFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
    depth_attachment.flags = 0;
    depth_attachment.format = _depthFormat;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { dependency, depth_dependency };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	//connect the color attachment to the info
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	//connect the subpass to the info
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_offscreenPass));

	_mainDeletionQueue.push_function([&]() {
		vkDestroyRenderPass(_device, _offscreenPass, nullptr);
	});
}

void VulkanEngine::init_default_renderpass()
{
	
	// the renderpass will use this color attachment.
	VkAttachmentDescription color_attachment = {};
	//the attachment will have the format needed by the swapchain
	color_attachment.format = _swapchainImageFormat;
	//1 sample, we won't be doing MSAA
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	// we Clear when this attachment is loaded
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// we keep the attachment stored when the renderpass ends
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//we don't care about stencil
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	//we don't know or care about the starting layout of the attachment
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	//after the renderpass ends, the image has to be on a layout ready for display
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	//attachment number will index into the pAttachments array in the parent renderpass itself
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
    // Depth attachment
    depth_attachment.flags = 0;
    depth_attachment.format = _depthFormat;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { dependency, depth_dependency };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	//connect the color attachment to the info
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	//connect the subpass to the info
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));

	_mainDeletionQueue.push_function([&]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
	});
}

void VulkanEngine::init_offscreen_framebuffers()
{
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	VkImageView attachments[2] = { _fullscreenImageView, _depthImageView };

	fb_info.renderPass = _offscreenPass;
	fb_info.attachmentCount = 2;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;
	fb_info.pAttachments = attachments;

	vkCreateFramebuffer(_device, &fb_info, nullptr, &_offscreenFramebuffer);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyFramebuffer(_device, _offscreenFramebuffer, nullptr);
	});

}

void VulkanEngine::init_framebuffers()
{
	//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	//grab how many images we have in the swapchain
	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	//create framebuffers for each of the swapchain image views
	for (int i = 0; i < swapchain_imagecount; i++) {

		VkImageView attachments[2];
		attachments[0] = _swapchainImageViews[i];
		attachments[1] = _depthImageView;

		fb_info.pAttachments = attachments;
		fb_info.attachmentCount = 2;
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

		_mainDeletionQueue.push_function([&]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		});
	}
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkFenceCreateInfo uploadCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(_device, &uploadCreateInfo, nullptr, &_uploadContext._uploadFence));
	_mainDeletionQueue.push_function([&]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
	});

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		_mainDeletionQueue.push_function([&]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
		});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		_mainDeletionQueue.push_function([&]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
		});
	}
}

std::string getShaderPath(const std::string& shaderName) {
	// Get the executable path
	std::filesystem::path exePath = std::filesystem::current_path();

	// Navigate to the shaders directory
	return (exePath / "shaders" / shaderName).string();
}

void VulkanEngine::init_pipelines()
{	
	build_material_default();
	build_material_water();
	build_material_wireframe();
}



void VulkanEngine::build_material_default()
{
	VkShaderModule trimeshFragShader;
	auto fragShaderPath = getShaderPath("tri_mesh.frag.spv");
	if (!load_shader_module(fragShaderPath, &trimeshFragShader))
	{
		fmt::println("Error when building the trimesh fragment shader module at: {}", fragShaderPath);
	}
	else {
		fmt::println("Trimesh fragment shader successfully loaded");
	}

	VkShaderModule trimeshVertexShader;
	auto vertexShaderPath = getShaderPath("tri_mesh.vert.spv");
	if (!load_shader_module(vertexShaderPath, &trimeshVertexShader))
	{
		fmt::println("Error when building the trimesh vertex shader module at: {}", vertexShaderPath);

	}
	else {
		fmt::println("Trimesh vertex shader successfully loaded");
	}

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorSetLayout setLayouts[] = { _uboSetLayout, _chunkSetLayout };

	//EXAMPLE: assigning descriptor set to this pipelien
	pipeline_layout_info.setLayoutCount = 2;
	pipeline_layout_info.pSetLayouts = setLayouts;

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:

	VkPushConstantRange push_constant = vkinit::pushconstrant_range(sizeof(ChunkPushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	pipeline_layout_info.pPushConstantRanges = &push_constant;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, trimeshVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, trimeshFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	create_material(meshPipeline, meshPipelineLayout, "defaultmesh");

	vkDestroyShaderModule(_device, trimeshFragShader, nullptr);
	vkDestroyShaderModule(_device, trimeshVertexShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipeline(_device, meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, meshPipelineLayout, nullptr);
	});
}

void VulkanEngine::build_material_water()
{
	VkShaderModule waterFragShader;
	auto fragShaderPath = getShaderPath("water_mesh.frag.spv");
	if (!load_shader_module(fragShaderPath, &waterFragShader))
	{
		fmt::println("Error when building the trimesh fragment shader module at: {}", fragShaderPath);
	}
	else {
		fmt::println("Trimesh fragment shader successfully loaded");
	}

	VkShaderModule waterVertexShader;
	auto vertexShaderPath = getShaderPath("water_mesh.vert.spv");
	if (!load_shader_module(vertexShaderPath, &waterVertexShader))
	{
		fmt::println("Error when building the trimesh vertex shader module at: {}", vertexShaderPath);

	}
	else {
		fmt::println("Trimesh vertex shader successfully loaded");
	}

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorSetLayout setLayouts[] = { _uboSetLayout, _chunkSetLayout };

	//EXAMPLE: assigning descriptor set to this pipelien
	pipeline_layout_info.setLayoutCount = 2;
	pipeline_layout_info.pSetLayouts = setLayouts;

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:

	VkPushConstantRange push_constant = vkinit::pushconstrant_range(sizeof(ChunkPushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	pipeline_layout_info.pPushConstantRanges = &push_constant;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state_blending();


	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, waterVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, waterFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	create_material(meshPipeline, meshPipelineLayout, "watermesh");

	vkDestroyShaderModule(_device, waterFragShader, nullptr);
	vkDestroyShaderModule(_device, waterVertexShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipeline(_device, meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, meshPipelineLayout, nullptr);
	});
}

void VulkanEngine::build_material_wireframe()
{
	VkShaderModule wiremeshFragShader;
	if (!load_shader_module(getShaderPath("wire_mesh.frag.spv"), &wiremeshFragShader))
	{
		fmt::println("Error when building the triangle fragment shader module");
	}
	else {
		fmt::println("Wireframe fragment shader successfully loaded");
	}

	VkShaderModule wiremeshVertexShader;
	if (!load_shader_module(getShaderPath("wire_mesh.vert.spv"), &wiremeshVertexShader))
	{
		fmt::println("Error when building the triangle vertex shader module");

	}
	else {
		fmt::println("Wireframe vertex shader successfully loaded");
	}

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:
	// pipeline_layout_info.pPushConstantRanges = &push_constant;
	// pipeline_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, wiremeshVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, wiremeshFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	create_material(meshPipeline, meshPipelineLayout, "wireframe");

	vkDestroyShaderModule(_device, wiremeshFragShader, nullptr);
	vkDestroyShaderModule(_device, wiremeshVertexShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipeline(_device, meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, meshPipelineLayout, nullptr);
	});


}

void VulkanEngine::build_postprocess_pipeline()
{
	VkShaderModule computeShaderModule;
	if (!load_shader_module(getShaderPath("fog.comp.spv"), &computeShaderModule))
	{
		fmt::println("Error when building the triangle fragment shader module");
	}
	else {
		fmt::println("Wireframe fragment shader successfully loaded");
	}


	// Assuming you already have a `VkDevice` and a compute shader module `VkShaderModule computeShaderModule`
	VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
	computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computeShaderStageInfo.module = computeShaderModule;
	computeShaderStageInfo.pName = "main";

	// Define pipeline layout (descriptor set layouts need to be set up based on your compute shader)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1; // Assuming one descriptor set layout
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; // Descriptor set layout containing the image resources

	if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_computeMaterial.pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline layout!");
	}

	// Create compute pipeline
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = computeShaderStageInfo;
	pipelineInfo.layout = _computeMaterial.pipelineLayout;

	if (vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_computeMaterial.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline!");
	}
}


AllocatedBuffer VulkanEngine::create_buffer(size_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage)
{
	VkBufferCreateInfo bufferInfo = vkinit::buffer_create_info(size, bufferUsage);

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memUsage;

	AllocatedBuffer buffer = {};

	vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
		&buffer._buffer,
		&buffer._allocation,
		nullptr);

	return buffer;
}

void VulkanEngine::upload_mesh(Mesh& mesh)
{
	AllocatedBuffer vertexStagingBuffer = create_buffer(mesh._vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	AllocatedBuffer indexStagingBuffer = create_buffer(mesh._indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	//copy vertex data
	void* vertexData;
	vmaMapMemory(_allocator, vertexStagingBuffer._allocation, &vertexData);
	memcpy(vertexData, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, vertexStagingBuffer._allocation);

	void* indexData;
	vmaMapMemory(_allocator, indexStagingBuffer._allocation, &indexData);
	memcpy(indexData, mesh._indices.data(), mesh._indices.size() * sizeof(uint32_t));
	vmaUnmapMemory(_allocator, indexStagingBuffer._allocation);	

	mesh._vertexBuffer = create_buffer(mesh._vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	mesh._indexBuffer = create_buffer(mesh._indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

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

void VulkanEngine::unload_mesh(std::shared_ptr<Mesh>&& mesh)
{
	if(mesh.use_count() == 1)
	{
		vmaDestroyBuffer(_allocator, mesh->_vertexBuffer._buffer, mesh->_vertexBuffer._allocation);
		vmaDestroyBuffer(_allocator, mesh->_indexBuffer._buffer, mesh->_indexBuffer._allocation);
	} else {
		throw std::runtime_error("Attempted to unload a mesh that is referenced elsewhere.");
	}
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function)
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
	VK_CHECK(vkQueueSubmit(_transferQueue, 1, &submit, _uploadContext._uploadFence));

	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	// reset the command buffers inside the command pool
	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}

FrameData &VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;
	return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string &name)
{
    //search for the object, and return nullptr if not found
	auto it = _materials.find(name);
	if (it == _materials.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject *first, int count)
{
	// Mesh* lastMesh = nullptr;
	// Material* lastMaterial = nullptr;
	// for (int i = 0; i < count; i++)
	// {
	// 	RenderObject& object = first[i];

	// 	//only bind the pipeline if it doesn't match with the already bound one
	// 	if (object.material != lastMaterial) {

	// 		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
	// 		lastMaterial = object.material;
	// 	}


	// 	// glm::mat4 model = object.transformMatrix;
	// 	// //final render matrix, that we are calculating on the cpu
	// 	// glm::mat4 mesh_matrix = _camera._projection * _camera._view * model;

	// 	// MeshPushConstants constants;
	// 	// constants.render_matrix = mesh_matrix;

	// 	//upload the mesh to the GPU via push constants
	// 	//vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);
	// 		//only bind the mesh if it's a different one from last bind
	// 	if (object.mesh.get() != lastMesh) {
	// 		//bind the mesh vertex buffer with offset 0
	// 		VkDeviceSize offset = 0;
	// 		vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);

	// 		vkCmdBindIndexBuffer(cmd, object.mesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

	// 		lastMesh = object.mesh.get();
	// 	}
	// 	//we can now draw
	// 	vkCmdDrawIndexed(cmd, object.mesh->_indices.size(), 1, 0, 0, 0);
	// }
}

void VulkanEngine::draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial)
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

void VulkanEngine::draw_chunks(VkCommandBuffer cmd)
{
	//Game is not finished with initial loading, do not render anything yet.
	if(_game._chunkManager._initLoad) return;

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;

	for(int i = 0; i < _game._chunkManager._renderedChunks.size(); i++)
	{
		auto& object = _game._chunkManager._renderedChunks[i];

		draw_object(cmd, object, lastMesh, lastMaterial);
	}

	for(int i = 0; i < _game._chunkManager._transparentObjects.size(); i++)
	{
		auto& object = _game._chunkManager._transparentObjects[i];

		draw_object(cmd, object, lastMesh, lastMaterial);
	}

}
