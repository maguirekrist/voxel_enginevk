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
	bool bUseValidationLayers{ false };
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

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//Transfer Queue
	VkQueue _transferQueue;
	uint32_t _transferQueueFamily;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	std::unordered_map<std::string, Material> _materials;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> _meshes;

	std::vector<RenderObject> _renderObjects;
	CubeEngine _game{ *this };
	Camera _camera;
	std::optional<RaycastResult> _targetBlock;

	float _deltaTime;
	TimePoint _lastFrameTime;
	TimePoint _lastFpsTime;
	float _fps;
	
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	FunctionQueue _mainDeletionQueue;
	moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUploadQueue;
	moodycamel::ConcurrentQueue<std::shared_ptr<Mesh>> _mainMeshUnloadQueue;
	moodycamel::ConcurrentQueue<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<SharedResource<Mesh>> > > _meshSwapQueue;
	VmaAllocator _allocator;

	VkDescriptorSetLayout _uboSetLayout;
	VkDescriptorSetLayout _chunkSetLayout;
	VkDescriptorPool _dPool;

	UploadContext _uploadContext;

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	FrameData _frames[FRAME_OVERLAP];

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	FrameData& get_current_frame();

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);

	AllocatedBuffer create_buffer(size_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage);

	void upload_mesh(Mesh& mesh);
	void unload_mesh(std::shared_ptr<Mesh>&& mesh);

	//??
	//Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	void draw_chunks(VkCommandBuffer cmd);

	void calculate_fps();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	void handle_input();

	//draw loop
	void draw();

	//run main loop
	void run();
private:

	VulkanEngine() {};

	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_descriptors();
	void init_default_renderpass();
	void init_framebuffers();
	void init_uniform_buffers();
	void init_sync_structures();
	void init_pipelines();

	void build_material_default();
	void build_material_wireframe();

	void update_uniform_buffers();
	void update_chunk_buffer();

	void submit_queue_present(VkCommandBuffer pCmd, uint32_t swapchainImageIndex); //takes in a primary command buffer only

	void handle_transfers();
	std::thread _transferThread;

	bool load_shader_module(const std::string& filePath, VkShaderModule* outShaderModule);

	void build_target_block_view(const glm::vec3& worldPos);
	RenderObject build_chunk_debug_view(const Chunk& chunk);
};


class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;

	VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};