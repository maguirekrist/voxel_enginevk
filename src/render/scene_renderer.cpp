#include "scene_renderer.h"
#include <vk_engine.h>
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <vk_mesh.h>

#include <scenes/game_scene.h>
#include <scenes/blueprint_builder_scene.h>

void SceneRenderer::init()
{
	_scenes["game"] = std::make_unique<GameScene>();
	//_scenes["blueprint"] = std::make_unique<BlueprintBuilderScene>();

	//set default scene
	_currentScene = _scenes["game"].get();
}

void SceneRenderer::render_scene(const VkCommandBuffer cmd, const uint32_t swapchainImageIndex)
{
    _currentScene->render(_renderQueue);

    VkRenderPassBeginInfo rpOffscreenInfo = vkinit::render_pass_begin_info(
        VulkanEngine::instance()._offscreenPass, 
        VulkanEngine::instance()._windowExtent, 
        VulkanEngine::instance()._offscreenFramebuffer, 
        VulkanEngine::instance()._clearColorAndDepth.data(),
        2);

    vkCmdBeginRenderPass(cmd, &rpOffscreenInfo, VK_SUBPASS_CONTENTS_INLINE);

    draw_objects(cmd, _renderQueue.getOpaqueQueue());

    vkCmdEndRenderPass(cmd);

    run_compute(cmd, VulkanEngine::instance()._materialManager.get_material("compute"));

	VkRenderPassBeginInfo rpInfo = vkinit::render_pass_begin_info(
        VulkanEngine::instance()._renderPass,
        VulkanEngine::instance()._windowExtent, 
        VulkanEngine::instance()._framebuffers[swapchainImageIndex], 
        VulkanEngine::instance()._clearColorOnly.data(), 
        1);

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    draw_fullscreen(cmd, VulkanEngine::instance()._materialManager.get_material("present"));

    //Draw transparent
    draw_objects(cmd, _renderQueue.getTransparentQueue());

    vkCmdEndRenderPass(cmd);

    _renderQueue.clear();
}

Scene* SceneRenderer::get_current_scene() const
{
    return _currentScene;
}

void SceneRenderer::cleanup()
{
    for(auto it = _scenes.begin(); it != _scenes.end(); )
    {
    	auto& scene = *it->second;
        scene.cleanup();

    	it = _scenes.erase(it);
    }
}

void SceneRenderer::run_compute(VkCommandBuffer cmd, const std::shared_ptr<Material>& computeMaterial)
{
    // Bind the compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeMaterial->pipeline);

    // Bind the descriptor set (which contains the image resources)
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computeMaterial->pipelineLayout,
        0, computeMaterial->descriptorSets.size(),

        computeMaterial->descriptorSets.data(),
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

void SceneRenderer::draw_fullscreen(const VkCommandBuffer cmd, const std::shared_ptr<Material>& presentMaterial)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, presentMaterial->pipeline);
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		presentMaterial->pipelineLayout,
		0, presentMaterial->descriptorSets.size(),
		presentMaterial->descriptorSets.data(),
		0, nullptr
	);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

void SceneRenderer::draw_object(const VkCommandBuffer cmd, const RenderObject& object)
{
	if(object.mesh == nullptr) return;
	if(!object.mesh->_isActive.load(std::memory_order_acquire)) return;

	//only bind the pipeline if it doesn't match with the already bound one
	// if (object.material->key != m_lastMaterialKey) {
	// 	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
	// 	m_lastMaterialKey = object.material->key;
	//
	// 	vkCmdBindDescriptorSets(cmd,
	// 	VK_PIPELINE_BIND_POINT_GRAPHICS,
	// 	object.material->pipelineLayout,
	// 	0,
	// 	object.material->descriptorSets.size(),
	// 	object.material->descriptorSets.data(),
	// 	0,
	// 	nullptr);
	// }

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
	m_lastMaterialKey = object.material->key;

	vkCmdBindDescriptorSets(cmd,
	VK_PIPELINE_BIND_POINT_GRAPHICS,
	object.material->pipelineLayout,
	0,
	object.material->descriptorSets.size(),
	object.material->descriptorSets.data(),
	0,
	nullptr);


	// ObjectPushConstants constants{};
	// constants.chunk_translate = object.xzPos;
	// vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ObjectPushConstants), &constants);


    for(const auto& pConstant : object.material->pushConstants)
    {
    	ObjectPushConstants data = pConstant.build_constant(object);
        vkCmdPushConstants(cmd, object.material->pipelineLayout, pConstant.stageFlags, 0, pConstant.size, &data);
    }
    

	//only bind the mesh if it's a different one from last bind
	if(object.mesh.get() != m_lastMesh) {
		//bind the mesh vertex buffer with offset 0
		constexpr VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);

		vkCmdBindIndexBuffer(cmd, object.mesh->_indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		m_lastMesh = object.mesh.get();
	}

	//we can now draw
	vkCmdDrawIndexed(cmd, object.mesh->_indices.size(), 1, 0, 0, 0);
}

void SceneRenderer::draw_objects(VkCommandBuffer cmd, const std::vector<const RenderObject*>& objects)
{
	for(const auto & object : objects)
	{
		if (object != nullptr)
		{
			draw_object(cmd, *object);
		} else
		{
			throw std::runtime_error("Object is null");
		}
	}
}
