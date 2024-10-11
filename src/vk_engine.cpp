
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
#include "scenes/game_scene.h"
#include "scenes/blueprint_builder_scene.h"

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

	init_scenes();

	_isInitialized = true;
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized) {

		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

		_descriptorAllocator.cleanup();

		_mainDeletionQueue.flush();

		//Handle deletion of meshes
		_game.cleanup();
		_meshManager.cleanup();
		

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
						//build_target_block_view(worldBlockPos);
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

void VulkanEngine::set_scene(Scene *scene)
{
	//Not sure if waiting for the fence is necessary here
	//VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	_currentScene = scene;
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

void VulkanEngine::update_fog_ubo()
{
	FogUBO fogUBO;
	fogUBO.fogColor = static_cast<glm::vec3>(Colors::skyblueHigh);
	fogUBO.fogEndColor = static_cast<glm::vec3>(Colors::skyblueLow);

	fogUBO.fogCenter = _game._player._position;
	fogUBO.fogRadius = (CHUNK_SIZE * DEFAULT_VIEW_DISTANCE) - 60.0f;
	fogUBO.screenSize = glm::ivec2(_windowExtent.width, _windowExtent.height);
	fogUBO.invViewProject = glm::inverse(_camera._projection * _camera._view);

	void* data;
	vmaMapMemory(_allocator, _fogUboBuffer._allocation, &data);
	memcpy(data, &fogUBO, sizeof(FogUBO));
	vmaUnmapMemory(_allocator, _fogUboBuffer._allocation);
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

//returns the swapChainImageIndex
uint32_t VulkanEngine::advance_frame()
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

	return swapchainImageIndex;
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
	
	uint32_t swapchainImageIndex = advance_frame();

	VkCommandBuffer cmd = begin_recording();

	_currentScene->render(cmd);
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

	_meshManager.init(_device, _allocator, { ._queue = _transferQueue, ._queueFamily = _transferQueueFamily });

	_descriptorAllocator.init(_device);
	_descriptorLayoutCache.init(_device);
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{_chosenGPU,_device,_surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_PASS_THROUGH_EXT})
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
	VkImageCreateInfo color_info = vkinit::image_create_info(_colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, windowImageExtent);
	VmaAllocationCreateInfo cimg_allocinfo = {};
	cimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	cimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vmaCreateImage(_allocator, &color_info, &cimg_allocinfo, &_fullscreenImage._image, &_fullscreenImage._allocation, nullptr);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo cview_info = vkinit::imageview_create_info(_colorFormat, _fullscreenImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &cview_info, nullptr, &_fullscreenImageView));

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyImageView(_device, _fullscreenImageView, nullptr);
		vmaDestroyImage(_allocator, _fullscreenImage._image, _fullscreenImage._allocation);
	});

	//CREATE DEPTH IMAGE

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	//the depth image will be an image with the format we selected and Depth Attachment usage flag
	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, windowImageExtent);

	//for the depth image, we want to allocate it from GPU local memory
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImageView));

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
	});

	//Initialize our sampler
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info();
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_sampler));

}


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
		_frames[i]._cameraBuffer = vkutil::create_buffer(_allocator, sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i]._chunkBuffer = vkutil::create_buffer(_allocator, sizeof(ChunkBufferObject) * MAXIMUM_CHUNKS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);



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
		_mainDeletionQueue.push_function([=, this]() {
			vmaDestroyBuffer(_allocator, _frames[i]._cameraBuffer._buffer, _frames[i]._cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i]._chunkBuffer._buffer, _frames[i]._chunkBuffer._allocation);
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

		_mainDeletionQueue.push_function([=, this]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
		});
	}

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
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

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

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyRenderPass(_device, _offscreenPass, nullptr);
	});
}

void VulkanEngine::init_default_renderpass()
{
	// the renderpass will use this color attachment.
	VkAttachmentDescription color_attachment = vkinit::attachment_description(_swapchainImageFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VkAttachmentReference color_attachment_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription depth_attachment = vkinit::attachment_description(_depthFormat, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    VkAttachmentReference depth_attachment_ref = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment_ref,
		.pDepthStencilAttachment = &depth_attachment_ref
	};

	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	VkSubpassDependency depth_dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
	};

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

	_mainDeletionQueue.push_function([=, this]() {
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

	_mainDeletionQueue.push_function([=, this]() {
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

		_mainDeletionQueue.push_function([=, this]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		});
	}
}

void VulkanEngine::init_scenes()
{
	_scenes["game"] = std::make_unique<GameScene>();
	_scenes["blueprint"] = std::make_unique<BlueprintBuilderScene>();

	//set default scene
	set_scene(_scenes["game"].get());
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		_mainDeletionQueue.push_function([=, this]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
		});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		_mainDeletionQueue.push_function([=, this]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
		});
	}
}

void VulkanEngine::init_pipelines()
{	
	_materialManager.build_postprocess_pipeline();
	_materialManager.build_material_default();
	_materialManager.build_material_water();
	_materialManager.build_material_wireframe();
	_materialManager.build_present_pipeline();
}

FrameData &VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}
