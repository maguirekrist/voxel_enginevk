#include "scene_renderer.h"
#include <vk_engine.h>
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <scenes/game_scene.h>

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

void SceneRenderer::init()
{
	_scenes["game"] = std::make_shared<GameScene>();
	_currentScene = _scenes["game"];
}

void SceneRenderer::cleanup()
{
	_scenes.clear();
	_currentScene = nullptr;
}

void SceneRenderer::render_scene(VkCommandBuffer cmd, const uint32_t swapchainImageIndex)
{
	ZoneScopedN("Render Scene");
	m_last_allocator = nullptr;
    _currentScene->update_buffers();

	if (USE_IMGUI)
	{
		_currentScene->draw_imgui();
	}

    VkRenderPassBeginInfo rpOffscreenInfo = vkinit::render_pass_begin_info(
        VulkanEngine::instance()._offscreenPass, 
        VulkanEngine::instance()._windowExtent, 
        VulkanEngine::instance()._offscreenFramebuffer, 
        VulkanEngine::instance()._clearColorAndDepth.data(),
        2);

    vkCmdBeginRenderPass(cmd, &rpOffscreenInfo, VK_SUBPASS_CONTENTS_INLINE);
    draw_objects(cmd, VulkanEngine::instance()._opaqueSet.data());

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
    draw_objects(cmd, VulkanEngine::instance()._transparentSet.data());

	if (USE_IMGUI)
	{
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

    vkCmdEndRenderPass(cmd);
}

std::shared_ptr<Scene> SceneRenderer::get_current_scene() const
{
	return _currentScene;
}

void SceneRenderer::run_compute(VkCommandBuffer cmd, const std::shared_ptr<Material>& computeMaterial)
{
	// Ensure images from the offscreen pass are properly visible/layouted for compute on MoltenVK
	// 1) Color image: offscreen render pass stores to GENERAL; make writes visible to compute shader writes
	// 2) Depth image: transition from attachment to read-only so compute can sample it

	// Barrier for color image (from color attachment writes to compute shader writes)
	{
		auto colorBarrier = vkinit::make_image_barrier({
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = VulkanEngine::instance()._fullscreenImage.image._image,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		});
		vkinit::cmd_image_barrier(
			cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			colorBarrier
		);
	}

	// Barrier for depth image (from depth attachment writes to shader read)
	{
		auto depthBarrier = vkinit::make_image_barrier({
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			.image = VulkanEngine::instance()._depthImage.image._image,
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
		});
		vkinit::cmd_image_barrier(
			cmd,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			depthBarrier
		);
	}

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

	// Make compute writes visible to fragment sampling of the fullscreen image
	{
		auto imgBarrier = vkinit::make_image_barrier({
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = VulkanEngine::instance()._fullscreenImage.image._image,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		});
		vkinit::cmd_image_barrier(
			cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			imgBarrier
		);
	}
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
	if(!object.mesh->_isActive.load(std::memory_order::acquire))
	{
		return;
	};

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

	//We only need to rebind these when the allocator changes.
	// if (m_last_allocator == nullptr || m_last_allocator != object.mesh->_vertexAllocation.allocator)
	// {
	// 	vkCmdBindVertexBuffers(
	// 		cmd,
	// 		0,
	// 		1,
	// 		&object.mesh->_vertexAllocation.allocator->m_vertexBuffer._buffer,
	// 		(VkDeviceSize[]){0}
	// 	);
	// 	vkCmdBindIndexBuffer(
	// 		cmd,
	// 		object.mesh->_vertexAllocation.allocator->m_indexBuffer._buffer,
	// 		0,
	// 		VK_INDEX_TYPE_UINT32
	// 	);
	//
	// 	m_last_allocator = object.mesh->_vertexAllocation.allocator;
	// }
	//
	// uint32_t firstIndex   = (uint32_t)(object.mesh->_indexAllocation.slot.index_offset  / sizeof(uint32_t));
	// int32_t  baseVertex   = (int32_t) (object.mesh->_vertexAllocation.slot.vertex_offset / sizeof(Vertex));
	//
	// // Sanity: offsets must be aligned
	// assert(object.mesh->_indexAllocation.slot.index_offset  % sizeof(uint32_t) == 0);
	// assert(object.mesh->_vertexAllocation.slot.vertex_offset % sizeof(Vertex)   == 0);
	//
	// //we can now draw
	// //question: how does draw Indexed work? How does it know index count of my vertices, is this computed based on the indices?
	// vkCmdDrawIndexed(
	// 	cmd,
	// 	static_cast<uint32_t>(object.mesh->_indices.size()),
	// 	1,
	// 	firstIndex,
	// 	baseVertex,
	// 	0
	// );

	VkDeviceSize vbOff = object.mesh->_allocation.slot.vertex_offset;
	VkDeviceSize ibOff =  object.mesh->_allocation.slot.index_offset;

	vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_allocation.allocator->m_vertexBuffer._buffer, &vbOff);
	vkCmdBindIndexBuffer(cmd, object.mesh->_allocation.allocator->m_indexBuffer._buffer, ibOff, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd,object.mesh->_allocation.indices_size, 1, 0, 0, 0);
}

void SceneRenderer::draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& objects)
{
	for(const auto & object : objects)
	{
		draw_object(cmd, object);
	}
}
