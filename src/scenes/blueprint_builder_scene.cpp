#include "blueprint_builder_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <vk_mesh.h>

BlueprintBuilderScene::BlueprintBuilderScene()
{
    fmt::println("BlueprintBuilderScene created!");

    // auto gridBuffer = vkutil::create_buffer(VulkanEngine::instance()._allocator, sizeof(FogUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    // _gridResource = Resource{Resource::BUFFER, { .buffer = gridBuffer }};
    //
    // auto cameraUboBuffer = vkutil::create_buffer(VulkanEngine::instance()._allocator, sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    // _cameraUboResource = Resource{Resource::BUFFER, {.buffer = cameraUboBuffer}};
}

void BlueprintBuilderScene::init() 
{
    // VulkanEngine::instance()._materialManager.build_material_grid(_cameraUboResource, _gridResource);

    // build_chunk_platform({0,0});
    // set_grid_uniform();
}

void BlueprintBuilderScene::render(RenderQueue& queue) {
    // Draw
    update_camera_uniform();
    //Build the chunk views
    //queue.add(_renderObjects, RenderLayer::Opaque);
}

void BlueprintBuilderScene::update(float deltaTime)
{
    //TODO: Update View

}

void BlueprintBuilderScene::handle_input(const SDL_Event &event)
{
}

void BlueprintBuilderScene::handle_keystate(const Uint8 *state)
{
}

void BlueprintBuilderScene::cleanup()
{
    _renderObjects.clear();
}

void BlueprintBuilderScene::build_chunk_platform(ChunkCoord coord)
{
    Mesh quadMesh = Mesh::create_quad_mesh();

    auto chunkPlatform = std::make_shared<RenderObject>(
        std::make_unique<Mesh>(std::move(quadMesh)),
        VulkanEngine::instance()._materialManager.get_material("grid"),
        glm::ivec2(coord.x,coord.z),
        RenderLayer::Opaque
    );

    _renderObjects.push_back(std::move(chunkPlatform));
}

void BlueprintBuilderScene::set_grid_uniform()
{
    GridUniform gridUBO{
        .gridSpacing = glm::ivec2(1, 1),
        .color = glm::vec3(1.0f, 1.0f, 1.0f),
        .backgroundColor = glm::vec3(0.0f, 0.0f, 0.0f)
    };

    void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, _gridResource->value.buffer._allocation, &data);
	memcpy(data, &gridUBO, sizeof(GridUniform));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, _gridResource->value.buffer._allocation);
}

void BlueprintBuilderScene::update_camera_uniform()
{
	CameraUBO cameraUBO;
	cameraUBO.projection = _camera._projection;
	cameraUBO.view = _camera._view;
	cameraUBO.viewproject = _camera._projection * _camera._view; 

	void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, _cameraUboResource->value.buffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, _cameraUboResource->value.buffer._allocation);
}