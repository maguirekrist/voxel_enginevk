// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <cube_engine.h>
#include <vulkan/vulkan_core.h>
#include <vk_util.h>
#include <render/mesh_manager.h>
#include <render/material_manager.h>
#include <render/scene_renderer.h>

class FunctionQueue {
public:
	
	void push_function(std::function<void()>&& function)
	{
		funcs.push_back(std::move(function));
	}

	void flush() {
		for(auto& func : funcs) {
			func();
		}

		funcs.clear();
	}

private:
	std::deque<std::function<void()>> funcs;
};

class VulkanEngine {
public:
	static VulkanEngine& instance()		
	{
		static VulkanEngine *instance = new VulkanEngine();
        return *instance;
	}

	bool _isInitialized{ false };
	int _frameNumber {0};

	bool bFocused = false;
	bool bQuit = false;
	bool bUseValidationLayers = USE_VALIDATION_LAYERS;
	
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
	VkSurfaceKHR _surface;
	VkPhysicalDeviceProperties _gpuProperties;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	//VkExtent2D _swapchainExtend;

	//Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	VkQueue _transferQueue;
	uint32_t _transferQueueFamily;

	MeshManager _meshManager;
	MaterialManager _materialManager;

	VkRenderPass _renderPass;
	VkRenderPass _offscreenPass;
	VkFramebuffer _offscreenFramebuffer;
	std::vector<VkFramebuffer> _framebuffers;

	SceneRenderer _sceneRenderer;

	float _deltaTime;
	TimePoint _lastFrameTime;
	TimePoint _lastFpsTime;
	float _fps;
	
	VkSampler _sampler;
	ImageResource _depthImage;
	VkFormat _depthFormat;
	VkFormat _colorFormat;
	ImageResource _fullscreenImage;

	FunctionQueue _mainDeletionQueue;

	VkClearValue _colorClear{ .color = {{ 0.0f, 0.0f, 0.0f, 1.0f }}};
	VkClearValue _depthClear{ .depthStencil = { .depth = 1.0f, .stencil = 0 }};

	std::array<VkClearValue, 2> _clearColorAndDepth{_colorClear, _depthClear};
	std::array<VkClearValue, 1> _clearColorOnly{_colorClear};

	VmaAllocator _allocator;

	vkutil::DescriptorAllocator _descriptorAllocator;
	vkutil::DescriptorLayoutCache _descriptorLayoutCache;

	VkDescriptorSetLayout _sampledImageSetLayout;
	VkDescriptorSet _sampledImageSet;

	FrameData _frames[FRAME_OVERLAP];

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	FrameData& get_current_frame();

	void calculate_fps();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	void handle_input();

	uint32_t advance_frame();

	VkCommandBuffer begin_recording();

	//draw loop
	void draw();

	//run main loop
	void run();
private:
	std::shared_ptr<std::mutex> m_queueMutex = nullptr;

	VulkanEngine();

	//General Vulkan Init for renderering
	void init_vulkan();
	void init_swapchain();
	void init_commands();

	//Initialize global image resources - required for offscreen image output 
	void init_offscreen_images();

	//Renderpass Init
	void init_offscreen_renderpass();
	void init_default_renderpass();

	//Frame Buffer Init
	void init_framebuffers();
	void init_offscreen_framebuffers();

	void init_sync_structures();

	void init_imgui();
	void imgui_upload_fonts();

	void submit_queue_present(VkCommandBuffer pCmd, uint32_t swapchainImageIndex); //takes in a primary command buffer only
};
