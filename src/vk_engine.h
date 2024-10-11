// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_types.h"
#include <condition_variable>
#include <vk_mesh.h>
#include <camera.h>
#include <cube_engine.h>
#include <vulkan/vulkan_core.h>
#include <utils/concurrentqueue.h>
#include <vk_util.h>
#include <render/mesh_manager.h>
#include <render/material_manager.h>
#include "scenes/scene.h"

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
	bool bUseValidationLayers{ true };
	int _frameNumber {0};

	bool bFocused = false;
	bool bQuit = false;
	
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

	Material _computeMaterial;

	//std::vector<RenderObject> _renderObjects;
	std::unordered_map<std::string, std::unique_ptr<Scene>> _scenes;
	CubeEngine _game{ *this };
	Camera _camera;
	Scene* _currentScene;
	std::optional<RaycastResult> _targetBlock;

	float _deltaTime;
	TimePoint _lastFrameTime;
	TimePoint _lastFpsTime;
	float _fps;
	
	VkSampler _sampler;
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	VkFormat _colorFormat;
	AllocatedImage _fullscreenImage;
	VkImageView _fullscreenImageView;

	FunctionQueue _mainDeletionQueue;
	moodycamel::ConcurrentQueue<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>> > > _meshSwapQueue;
	VmaAllocator _allocator;

	vkutil::DescriptorAllocator _descriptorAllocator;
	vkutil::DescriptorLayoutCache _descriptorLayoutCache;

	std::array<VkDescriptorSet, 3> _computeDescriptorSets;
	AllocatedBuffer _fogUboBuffer;
	VkDescriptorSet _fogSet;

	VkDescriptorSetLayout _sampledImageSetLayout;
	VkDescriptorSet _sampledImageSet;

	VkDescriptorSetLayout _uboSetLayout;
	VkDescriptorSetLayout _chunkSetLayout;
	VkDescriptorPool _dPool;

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

	void set_scene(Scene* scene);

	uint32_t advance_frame();

	VkCommandBuffer begin_recording();

	//draw loop
	void draw();

	//run main loop
	void run();
private:

	VulkanEngine() {};

	//General Vulkan Init for renderering
	void init_vulkan();
	void init_swapchain();
	void init_commands();

	//Descriptor Layout Init
	void init_descriptors();

	//Initialize global image resources - required for offscreen image output 
	void init_offscreen_images();

	//Renderpass Init
	void init_offscreen_renderpass();
	void init_default_renderpass();

	//Frame Buffer Init
	void init_framebuffers();
	void init_offscreen_framebuffers();
	//void init_uniform_buffers();

	void init_scenes();

	void init_sync_structures();

	//Pipeline creation
	void init_pipelines();

	//Buffer updates
	void update_uniform_buffers();
	void update_fog_ubo();
	void update_chunk_buffer();

	void draw_fullscreen(VkCommandBuffer cmd, Material* presentMaterial);

	void submit_queue_present(VkCommandBuffer pCmd, uint32_t swapchainImageIndex); //takes in a primary command buffer only


	//Test Functions
	// void build_target_block_view(const glm::vec3& worldPos);
	// RenderObject build_chunk_debug_view(const Chunk& chunk);
};
