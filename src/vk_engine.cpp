
#include "vk_engine.h" // patched test comment

#include <SDL.h>
#include <SDL_vulkan.h>

#include "constants.h"
#include "tracy/Tracy.hpp"

#include <cstdint>
#include <vk_initializers.h>
#include <sdl_utils.h>
#include <vulkan/vulkan_core.h>

#include <game/cube_engine.h>

#include "VkBootstrap.h"
#include "vk_types.h"

#define VMA_IMPLEMENTATION
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "vk_mem_alloc.h"


VulkanEngine::VulkanEngine()
{
}

void VulkanEngine::calculate_fps()
{
	std::chrono::duration<float> elapsedTime = Clock::now() - _lastFpsTime;

	if(elapsedTime.count() >= 1.0f)
	{
		_fps = _frameNumber / elapsedTime.count();
		std::println("FPS: {}", _fps);

		_frameNumber = 0;
		_lastFpsTime = Clock::now();
	}
}

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	
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

	if (USE_IMGUI)
	{
		init_imgui();
	}

	_meshManager.init(_device, _allocator, { ._queue = _transferQueue, ._queueFamily = _transferQueueFamily });

	_sceneRenderer.init();

	_isInitialized = true;
}

void VulkanEngine::cleanup()
{
	std::println("VulkanEngine::cleanup");
	if (_isInitialized) {

		VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

		_sceneRenderer.cleanup();
		_opaqueSet.clear();
		_transparentSet.clear();
		_meshManager.cleanup();
		_materialManager.cleanup();

		if (USE_IMGUI)
		{
			//ImGui cleanup
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();
		}

		_mainDeletionQueue.flush();
		_descriptorLayoutCache.cleanup();
		_descriptorAllocator.cleanup();

		vmaDestroyAllocator(_allocator);

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
		if (USE_IMGUI)
		{
			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		switch(e.type) {
			case SDL_WINDOWEVENT:
				if(e.window.event == SDL_WINDOWEVENT_RESIZED)
				{
					//TODO: Handle resize
					std::println("RESIZED!");
					bResizeRequest = true;
					//throw std::runtime_error("RESIZED!");
				}
				break;
			case SDL_KEYDOWN:
				switch(e.key.keysym.sym)
				{
					case SDLK_ESCAPE:
						bFocused = false;
						SDL_SetWindowGrab(_window, SDL_FALSE);
						SDL_SetRelativeMouseMode(SDL_FALSE);
						SDL_ShowCursor(SDL_TRUE);
						break;
					default:
						//no-op
						break;
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(bFocused == false)
				{
					bFocused = true;
					SDL_SetWindowGrab(_window, SDL_TRUE);
					SDL_SetRelativeMouseMode(SDL_TRUE);
					SDL_ShowCursor(SDL_FALSE);
				}
				break;
			case SDL_QUIT:
				bQuit = true;
				break;
			default:
				//no-op
				break;
		}

		if(bFocused) {
			_sceneRenderer.get_current_scene()->handle_input(e);
		}
	}

	_sceneRenderer.get_current_scene()->handle_keystate(state);
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
		VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._presentSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);
		if (e == VK_ERROR_OUT_OF_DATE_KHR) {
			bResizeRequest = true;
			return 0;
		}
		else {
			VK_CHECK(e);
		}
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
	_meshManager.unload_garbage();
	_meshManager.handle_transfers();
	VkCommandBuffer cmd = begin_recording();

	_sceneRenderer.render_scene(cmd, swapchainImageIndex);

	VK_CHECK(vkEndCommandBuffer(cmd));
	submit_queue_present(cmd, swapchainImageIndex);
	//increase the number of frames drawn
	_frameNumber++;
}

void VulkanEngine::submit_queue_present(const VkCommandBuffer pCmd, const uint32_t swapchainImageIndex)
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
	submit.pSignalSemaphores = &_imageRenderFinishedSemaphores[swapchainImageIndex];

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &pCmd;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

	// this will put the image we just rendered into the visible window.
	// we want to wait on the _renderSemaphore for that,
	// as it's necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_imageRenderFinishedSemaphores[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;
	
	VkResult e = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
		bResizeRequest = true;
		return;
	}
}

void VulkanEngine::run()
{

	//main loop
	while (!bQuit)
	{
		ZoneScopedN("Main Loop");
		TimePoint now = std::chrono::steady_clock::now();
		std::chrono::duration<float> test = now - _lastFrameTime;
		_deltaTime = test.count();
		_lastFrameTime = now;

		handle_input();

		_sceneRenderer.get_current_scene()->update(_deltaTime);

		if (bResizeRequest) {
			resize_swapchain();
			continue;
		}

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


	std::println("Has dedicated transfer queue? {}", physicalDevice.has_dedicated_transfer_queue());


	//create the final vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build()
		.value();

	_gpuProperties = vkbDevice.physical_device.properties;

	std::println("The GPU has a minimum buffer alignment of {}", _gpuProperties.limits.minUniformBufferOffsetAlignment);
	std::println("Max workgroup size {}", _gpuProperties.limits.maxComputeWorkGroupSize);
	std::println("Max workgroup invocations: {}", _gpuProperties.limits.maxComputeWorkGroupInvocations);
	//std::println("The GPU has a nonCoherentAtomSize of {}", _gpuProperties.limits.nonCoherentAtomSize);

	auto queue_families = vkbDevice.queue_families;

	// Get the VkDevice handle used in the rest of a vulkan application
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;


	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VkQueueFamilyProperties graphicsQueueProps;

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
	// Per-image render-finished semaphores to prevent reuse across different images
	_imageRenderFinishedSemaphores.resize(_swapchainImages.size());
	VkSemaphoreCreateInfo perImageSemaphoreInfo = vkinit::semaphore_create_info();
	for(size_t i = 0; i < _imageRenderFinishedSemaphores.size(); ++i) {
		VK_CHECK(vkCreateSemaphore(_device, &perImageSemaphoreInfo, nullptr, &_imageRenderFinishedSemaphores[i]));
		_mainDeletionQueue.push_function([=, this]() { vkDestroySemaphore(_device, _imageRenderFinishedSemaphores[i], nullptr); });
	}

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

	const AllocatedImage fullscreenImage = vkutil::create_image(_allocator, windowImageExtent, _colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo cview_info = vkinit::imageview_create_info(_colorFormat, fullscreenImage._image, VK_IMAGE_ASPECT_COLOR_BIT);

	VkImageView fullscreenImageView;
	VK_CHECK(vkCreateImageView(_device, &cview_info, nullptr, &fullscreenImageView));

	//_mainDeletionQueue.push_function([=, this]() {
	//	vkDestroyImageView(_device, fullscreenImageView, nullptr);
	//	vmaDestroyImage(_allocator, fullscreenImage._image, fullscreenImage._allocation);
	//});

	_fullscreenImage = {
		.image = fullscreenImage,
		.view = fullscreenImageView
	};

	//CREATE DEPTH IMAGE
	_depthFormat = VK_FORMAT_D32_SFLOAT;

	auto depthImage = vkutil::create_image(_allocator, windowImageExtent, _depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VkImageView depthImageView;
	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &depthImageView));

	//_mainDeletionQueue.push_function([=, this]() {
	//	vkDestroyImageView(_device, depthImageView, nullptr);
	//	vmaDestroyImage(_allocator, depthImage._image, depthImage._allocation);
	//});

	_depthImage = {
		.image = depthImage,
		.view = depthImageView
	};

	//Initialize our sampler
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info();
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_sampler));

	//_mainDeletionQueue.push_function([=, this]() {
	//	vkDestroySampler(_device, _sampler, nullptr);
	//});
}

void VulkanEngine::destroy_offscreen_images()
{
	vkDestroyImageView(_device, _depthImage.view, nullptr);
	vmaDestroyImage(_allocator, _depthImage.image._image, _depthImage.image._allocation);
	vkDestroyImageView(_device, _fullscreenImage.view, nullptr);
	vmaDestroyImage(_allocator, _fullscreenImage.image._image, _fullscreenImage.image._allocation);
	vkDestroySampler(_device, _sampler, nullptr);
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

	VkAttachmentReference color_attachment_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription depth_attachment = {};
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

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

	VkImageView attachments[2] = { _fullscreenImage.view, _depthImage.view };

	fb_info.renderPass = _offscreenPass;
	fb_info.attachmentCount = 2;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;
	fb_info.pAttachments = attachments;

	vkCreateFramebuffer(_device, &fb_info, nullptr, &_offscreenFramebuffer);

	/*_mainDeletionQueue.push_function([=, this]() {
		vkDestroyFramebuffer(_device, _offscreenFramebuffer, nullptr);
	});*/

}

void VulkanEngine::destroy_offscreen_framebuffers()
{
	vkDestroyFramebuffer(_device, _offscreenFramebuffer, nullptr);
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
		attachments[1] = _depthImage.view;

		fb_info.pAttachments = attachments;
		fb_info.attachmentCount = 2;
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

		//_mainDeletionQueue.push_function([=, this]() {
		//	vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
		//	vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		//});
	}
}

void VulkanEngine::destroy_framebuffers()
{
	for (int i = 0; i < _framebuffers.size(); i++) {
		vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
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

void VulkanEngine::init_imgui()
{
	//SETUP IMgui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplSDL2_InitForVulkan(_window);

	//Optionally enable docking, etc.
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	std::println("SwapChain image count: {}", _swapchainImages.size());



	ImGui::StyleColorsDark();
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.QueueFamily = _graphicsQueueFamily;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = _descriptorAllocator.grab_pool();
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.RenderPass = _renderPass;
	init_info.ApiVersion = VK_API_VERSION_1_1;
	init_info.MinImageCount = _swapchainImages.size();
	init_info.ImageCount = _swapchainImages.size();
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.Allocator = nullptr;

	ImGui_ImplVulkan_Init(&init_info);
}


void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);
}


void VulkanEngine::resize_swapchain()
{
	vkDeviceWaitIdle(_device);

	destroy_swapchain();
	destroy_offscreen_images();
	destroy_framebuffers();
	destroy_offscreen_framebuffers();

	int w, h;
	SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width = w;
	_windowExtent.height = h;

	init_swapchain();
	init_offscreen_images();
	init_framebuffers();
	init_offscreen_framebuffers();

	_sceneRenderer.get_current_scene()->rebuild_pipelines();

	bResizeRequest = false;
}


FrameData &VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}
