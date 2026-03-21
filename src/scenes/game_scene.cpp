#include "game_scene.h"
#include "vk_engine.h"
#include <array>
#include <format>
#include <limits>
#include <ranges>
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "render/mesh_release_queue.h"

namespace
{
    bool equal_spline_points(const std::vector<SplinePoint>& lhs, const std::vector<SplinePoint>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i].noiseValue != rhs[i].noiseValue || lhs[i].heightValue != rhs[i].heightValue)
            {
                return false;
            }
        }

        return true;
    }

    bool equal_world_gen_settings(const TerrainGeneratorSettings& lhs, const TerrainGeneratorSettings& rhs)
    {
        return lhs.seed == rhs.seed &&
            lhs.shape.terrainFrequency == rhs.shape.terrainFrequency &&
            lhs.shape.climateFrequency == rhs.shape.climateFrequency &&
            lhs.shape.riverFrequency == rhs.shape.riverFrequency &&
            lhs.shape.riverThreshold == rhs.shape.riverThreshold &&
            lhs.shape.erosionSuppressionLow == rhs.shape.erosionSuppressionLow &&
            lhs.shape.erosionSuppressionHigh == rhs.shape.erosionSuppressionHigh &&
            lhs.biome.oceanContinentalnessThreshold == rhs.biome.oceanContinentalnessThreshold &&
            lhs.biome.riverBlendThreshold == rhs.biome.riverBlendThreshold &&
            lhs.biome.riverMinBankHeightOffset == rhs.biome.riverMinBankHeightOffset &&
            lhs.biome.beachMinHeightOffset == rhs.biome.beachMinHeightOffset &&
            lhs.biome.beachMaxHeightOffset == rhs.biome.beachMaxHeightOffset &&
            lhs.biome.mountainHeightOffset == rhs.biome.mountainHeightOffset &&
            lhs.biome.mountainPeaksThreshold == rhs.biome.mountainPeaksThreshold &&
            lhs.biome.forestHumidityThreshold == rhs.biome.forestHumidityThreshold &&
            lhs.biome.forestTemperatureThreshold == rhs.biome.forestTemperatureThreshold &&
            lhs.biome.mountainStoneHeightOffset == rhs.biome.mountainStoneHeightOffset &&
            lhs.surface.riverTargetHeightOffset == rhs.surface.riverTargetHeightOffset &&
            lhs.surface.riverMinDepth == rhs.surface.riverMinDepth &&
            lhs.surface.riverMaxDepth == rhs.surface.riverMaxDepth &&
            lhs.surface.oceanFloorHeightOffset == rhs.surface.oceanFloorHeightOffset &&
            lhs.surface.shoreMinHeightOffset == rhs.surface.shoreMinHeightOffset &&
            lhs.surface.shoreMaxHeightOffset == rhs.surface.shoreMaxHeightOffset &&
            lhs.surface.riverStoneDepth == rhs.surface.riverStoneDepth &&
            lhs.surface.oceanStoneDepth == rhs.surface.oceanStoneDepth &&
            lhs.surface.shoreStoneDepth == rhs.surface.shoreStoneDepth &&
            lhs.surface.plainsStoneDepth == rhs.surface.plainsStoneDepth &&
            lhs.surface.mountainStoneDepth == rhs.surface.mountainStoneDepth &&
            equal_spline_points(lhs.erosionSplines, rhs.erosionSplines) &&
            equal_spline_points(lhs.peakSplines, rhs.peakSplines) &&
            equal_spline_points(lhs.continentalSplines, rhs.continentalSplines);
    }

    float sample_spline_height(const std::vector<SplinePoint>& spline, const float noiseValue)
    {
        if (spline.empty())
        {
            return 0.0f;
        }

        for (size_t i = 0; i + 1 < spline.size(); ++i)
        {
            if (noiseValue >= spline[i].noiseValue && noiseValue <= spline[i + 1].noiseValue)
            {
                const float range = spline[i + 1].noiseValue - spline[i].noiseValue;
                const float t = std::abs(range) > std::numeric_limits<float>::epsilon()
                    ? (noiseValue - spline[i].noiseValue) / range
                    : 0.0f;
                return lerp(spline[i].heightValue, spline[i + 1].heightValue, t);
            }
        }

        return noiseValue < spline.front().noiseValue ? spline.front().heightValue : spline.back().heightValue;
    }

    const char* biome_label(const BiomeType biome)
    {
        switch (biome)
        {
        case BiomeType::Ocean: return "Ocean";
        case BiomeType::Shore: return "Shore";
        case BiomeType::Plains: return "Plains";
        case BiomeType::Forest: return "Forest";
        case BiomeType::River: return "River";
        case BiomeType::Mountains: return "Mountains";
        }

        return "Unknown";
    }

    ImU32 pack_color(const ImVec4 color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    ImVec4 gradient_color(const float normalized)
    {
        const float t = std::clamp(normalized, 0.0f, 1.0f);
        if (t < 0.33f)
        {
            const float localT = t / 0.33f;
            return ImVec4(0.08f + (0.12f * localT), 0.12f + (0.46f * localT), 0.20f + (0.50f * localT), 1.0f);
        }

        if (t < 0.66f)
        {
            const float localT = (t - 0.33f) / 0.33f;
            return ImVec4(0.20f + (0.38f * localT), 0.58f + (0.26f * localT), 0.70f - (0.32f * localT), 1.0f);
        }

        const float localT = (t - 0.66f) / 0.34f;
        return ImVec4(0.58f + (0.34f * localT), 0.84f - (0.12f * localT), 0.38f + (0.16f * localT), 1.0f);
    }

    ImU32 noise_preview_color(const TerrainColumnSample& column, const int layer)
    {
        switch (layer)
        {
        case 0:
            return pack_color(gradient_color(static_cast<float>(column.surfaceHeight) / static_cast<float>(CHUNK_HEIGHT - 1)));
        case 1:
            return pack_color(gradient_color((column.noise.continentalness + 1.0f) * 0.5f));
        case 2:
            return pack_color(gradient_color((column.noise.erosion + 1.0f) * 0.5f));
        case 3:
            return pack_color(gradient_color((column.noise.peaksValleys + 1.0f) * 0.5f));
        case 4:
            return pack_color(gradient_color((column.noise.temperature + 1.0f) * 0.5f));
        case 5:
            return pack_color(gradient_color((column.noise.humidity + 1.0f) * 0.5f));
        case 6:
            return pack_color(gradient_color((column.noise.river + 1.0f) * 0.5f));
        case 7:
            switch (column.biome)
            {
            case BiomeType::Ocean: return IM_COL32(28, 88, 160, 255);
            case BiomeType::Shore: return IM_COL32(212, 196, 132, 255);
            case BiomeType::Plains: return IM_COL32(106, 170, 88, 255);
            case BiomeType::Forest: return IM_COL32(48, 118, 54, 255);
            case BiomeType::River: return IM_COL32(70, 142, 204, 255);
            case BiomeType::Mountains: return IM_COL32(120, 124, 132, 255);
            }
            break;
        default:
            break;
        }

        return IM_COL32(255, 255, 255, 255);
    }

    void draw_spline_editor(const char* tableId, const char* label, std::vector<SplinePoint>& spline)
    {
        ImGui::PushID(tableId);
        ImGui::SeparatorText(label);

        std::array<float, 64> preview{};
        for (size_t i = 0; i < preview.size(); ++i)
        {
            const float noise = -1.0f + (2.0f * static_cast<float>(i) / static_cast<float>(preview.size() - 1));
            preview[i] = sample_spline_height(spline, noise);
        }
        ImGui::PlotLines("##SplinePreview", preview.data(), static_cast<int>(preview.size()), 0, nullptr, 0.0f, static_cast<float>(CHUNK_HEIGHT - 1), ImVec2(-1.0f, 70.0f));

        int removeIndex = -1;
        if (ImGui::BeginTable(tableId, 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Noise");
            ImGui::TableSetupColumn("Height");
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(spline.size()); ++i)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SliderFloat("##noise", &spline[i].noiseValue, -1.0f, 1.0f, "%.2f");

                ImGui::TableSetColumnIndex(1);
                ImGui::SliderFloat("##height", &spline[i].heightValue, 0.0f, static_cast<float>(CHUNK_HEIGHT - 1), "%.1f");

                ImGui::TableSetColumnIndex(2);
                if (spline.size() > 2)
                {
                    if (ImGui::SmallButton("Remove"))
                    {
                        removeIndex = i;
                    }
                }
                else
                {
                    ImGui::TextDisabled("Min 2");
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (removeIndex >= 0)
        {
            spline.erase(spline.begin() + removeIndex);
        }

        if (ImGui::Button(std::format("Add Point##{}", tableId).c_str()))
        {
            const SplinePoint tail = spline.empty() ? SplinePoint{} : spline.back();
            spline.push_back(SplinePoint{
                .noiseValue = std::clamp(tail.noiseValue + 0.15f, -1.0f, 1.0f),
                .heightValue = std::clamp(tail.heightValue + 8.0f, 0.0f, static_cast<float>(CHUNK_HEIGHT - 1))
            });
        }
        ImGui::SameLine();
        if (ImGui::Button(std::format("Sort By Noise##{}", tableId).c_str()))
        {
            std::ranges::sort(spline, [](const SplinePoint& lhs, const SplinePoint& rhs)
            {
                return lhs.noiseValue < rhs.noiseValue;
            });
        }
        ImGui::PopID();
    }

    void draw_noise_preview(const char* label, const ChunkCoord centerChunk, const int viewDistance, const int layer)
    {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const int chunkSpan = (viewDistance * 2) + 1;
        const int worldSpan = chunkSpan * static_cast<int>(CHUNK_SIZE);
        const float previewSize = std::max(192.0f, std::min(availableWidth, 420.0f));
        const float cellSize = previewSize / static_cast<float>(worldSpan);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 end = ImVec2(start.x + previewSize, start.y + previewSize);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        TerrainGenerator& terrainGenerator = TerrainGenerator::instance();

        drawList->AddRectFilled(start, end, IM_COL32(18, 20, 24, 255), 6.0f);
        for (int chunkOffsetZ = -viewDistance; chunkOffsetZ <= viewDistance; ++chunkOffsetZ)
        {
            for (int chunkOffsetX = -viewDistance; chunkOffsetX <= viewDistance; ++chunkOffsetX)
            {
                const ChunkCoord chunkCoord{ centerChunk.x + chunkOffsetX, centerChunk.z + chunkOffsetZ };
                const ChunkTerrainData chunkData = terrainGenerator.GenerateChunkData(
                    chunkCoord.x * static_cast<int>(CHUNK_SIZE),
                    chunkCoord.z * static_cast<int>(CHUNK_SIZE));

                for (int localZ = 0; localZ < CHUNK_SIZE; ++localZ)
                {
                    for (int localX = 0; localX < CHUNK_SIZE; ++localX)
                    {
                        const int worldX = ((chunkOffsetX + viewDistance) * static_cast<int>(CHUNK_SIZE)) + localX;
                        const int worldZ = ((chunkOffsetZ + viewDistance) * static_cast<int>(CHUNK_SIZE)) + localZ;
                        const TerrainColumnSample& column = chunkData.at(localX, localZ);
                        const ImVec2 min(start.x + (static_cast<float>(worldX) * cellSize), start.y + (static_cast<float>(worldZ) * cellSize));
                        const ImVec2 max(min.x + cellSize + 1.0f, min.y + cellSize + 1.0f);
                        drawList->AddRectFilled(min, max, noise_preview_color(column, layer));
                    }
                }

                const float chunkMinX = start.x + static_cast<float>((chunkOffsetX + viewDistance) * static_cast<int>(CHUNK_SIZE)) * cellSize;
                const float chunkMinY = start.y + static_cast<float>((chunkOffsetZ + viewDistance) * static_cast<int>(CHUNK_SIZE)) * cellSize;
                const float chunkMaxX = chunkMinX + (static_cast<float>(CHUNK_SIZE) * cellSize);
                const float chunkMaxY = chunkMinY + (static_cast<float>(CHUNK_SIZE) * cellSize);
                drawList->AddRect(
                    ImVec2(chunkMinX, chunkMinY),
                    ImVec2(chunkMaxX, chunkMaxY),
                    chunkCoord == centerChunk ? IM_COL32(255, 245, 170, 200) : IM_COL32(255, 255, 255, 36),
                    0.0f,
                    0,
                    chunkCoord == centerChunk ? 2.0f : 1.0f);
            }
        }

        ImGui::InvisibleButton(label, ImVec2(previewSize, previewSize));
        if (ImGui::IsItemHovered())
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int previewX = std::clamp(static_cast<int>((mouse.x - start.x) / cellSize), 0, worldSpan - 1);
            const int previewZ = std::clamp(static_cast<int>((mouse.y - start.y) / cellSize), 0, worldSpan - 1);
            const int chunkOffsetX = (previewX / static_cast<int>(CHUNK_SIZE)) - viewDistance;
            const int chunkOffsetZ = (previewZ / static_cast<int>(CHUNK_SIZE)) - viewDistance;
            const int localX = previewX % static_cast<int>(CHUNK_SIZE);
            const int localZ = previewZ % static_cast<int>(CHUNK_SIZE);
            const ChunkCoord hoveredChunk{ centerChunk.x + chunkOffsetX, centerChunk.z + chunkOffsetZ };
            const ChunkTerrainData chunkData = terrainGenerator.GenerateChunkData(
                hoveredChunk.x * static_cast<int>(CHUNK_SIZE),
                hoveredChunk.z * static_cast<int>(CHUNK_SIZE));
            const TerrainColumnSample& column = chunkData.at(localX, localZ);
            const int worldX = hoveredChunk.x * static_cast<int>(CHUNK_SIZE) + localX;
            const int worldZ = hoveredChunk.z * static_cast<int>(CHUNK_SIZE) + localZ;

            ImGui::BeginTooltip();
            ImGui::Text("Chunk: %d, %d", hoveredChunk.x, hoveredChunk.z);
            ImGui::Text("World: %d, %d", worldX, worldZ);
            ImGui::Text("Local: %d, %d", localX, localZ);
            ImGui::Text("Height: %d", column.surfaceHeight);
            ImGui::Text("Biome: %s", biome_label(column.biome));
            ImGui::Text("Cont: %.2f", column.noise.continentalness);
            ImGui::Text("Erosion: %.2f", column.noise.erosion);
            ImGui::Text("Peaks: %.2f", column.noise.peaksValleys);
            ImGui::Text("Temp: %.2f", column.noise.temperature);
            ImGui::Text("Humidity: %.2f", column.noise.humidity);
            ImGui::Text("River: %.2f", column.noise.river);
            ImGui::EndTooltip();
        }
    }
}


GameScene::GameScene(const SceneServices& services): _services(services)
{
    const ResourceBackendContext resourceBackend{
        .device = _services.device,
        .allocator = _services.allocator
    };
	auto fogUboBuffer = vkutil::create_buffer(_services.allocator, sizeof(FogUBO),
	                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	auto cameraUboBuffer = vkutil::create_buffer(_services.allocator, sizeof(CameraUBO),
	                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    auto lightingUboBuffer = vkutil::create_buffer(_services.allocator, sizeof(LightingUBO),
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_cameraUboResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(cameraUboBuffer));
	_fogResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(fogUboBuffer));
    _lightingResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(lightingUboBuffer));

	build_pipelines();

	create_camera();
    if (_services.configService != nullptr)
    {
        TerrainGenerator::instance().apply_settings(_services.configService->world_gen().load_or_default());
    }
    bind_settings();
    sync_world_gen_draft();
    sync_camera_to_game(0.0f);

	std::println("GameScene created!");
}

GameScene::~GameScene()
{
    clear_target_block_outline();
    clear_chunk_boundary_debug();
	_chunkRenderRegistry.clear(_renderState);
    if (_chunkBoundaryMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_chunkBoundaryMesh));
    }
    if (_targetBlockOutlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_targetBlockOutlineMesh));
    }
	std::println("GameScene::~GameScene");
}

void GameScene::update_buffers() {
	ZoneScopedN("Draw Chunks & Objects");
	_chunkRenderRegistry.sync(
		_game.chunk_manager(),
		*_services.meshManager,
		*_services.materialManager,
		_renderState);
    sync_target_block_outline();
    sync_chunk_boundary_debug();
	update_uniform_buffer();
    update_lighting_ubo();
	update_fog_ubo();
}

SceneRenderState& GameScene::get_render_state()
{
	return _renderState;
}

void GameScene::update(const float deltaTime)
{
	_game.set_player_input(_playerInput);
    _playerInput.lookDeltaX = 0.0f;
    _playerInput.lookDeltaY = 0.0f;
	_game.update(deltaTime);
    auto& dayNight = _settings.persistence().dayNight;
    if (!dayNight.paused)
    {
        _settings.mutate([deltaTime](settings::GameSettingsPersistence& persistence)
        {
            const float cycleDuration = std::max(persistence.dayNight.tuning.cycleDurationSeconds, 1.0f);
            persistence.dayNight.timeOfDay = std::fmod(persistence.dayNight.timeOfDay + (deltaTime / cycleDuration), 1.0f);
        });
    }
    sync_camera_to_game(deltaTime);
    sync_target_block();
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
        case SDL_KEYDOWN:
            if (!event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_G)
            {
                _settings.mutate([](settings::GameSettingsPersistence& persistence)
                {
                    persistence.debug.showChunkBoundaries = !persistence.debug.showChunkBoundaries;
                });
            }
            break;
		case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT && _targetBlock.has_value())
            {
                if (_targetBlock->_distance <= GameConfig::BLOCK_INTERACTION_DISTANCE)
                {
                    _game.apply_block_edit(BlockEdit{
                        .worldPos = _targetBlock->_worldPos,
                        .newBlock = Block{
                            ._solid = false,
                            ._sunlight = 0,
                            ._type = BlockType::AIR
                        },
                        .source = EditSource::LocalPlayer
                    });
                }
            }
            else if (event.button.button == SDL_BUTTON_RIGHT && _targetBlock.has_value())
            {
                if (_targetBlock->_distance <= GameConfig::BLOCK_INTERACTION_DISTANCE)
                {
                    const glm::ivec3 placePos = _targetBlock->_worldPos + glm::ivec3(
                        faceOffsetX[_targetBlock->_blockFace],
                        faceOffsetY[_targetBlock->_blockFace],
                        faceOffsetZ[_targetBlock->_blockFace]);
                    _game.apply_block_edit(BlockEdit{
                        .worldPos = placePos,
                        .newBlock = Block{
                            ._solid = true,
                            ._sunlight = 0,
                            ._type = BlockType::LAMP
                        },
                        .source = EditSource::LocalPlayer
                    });
                }
            }
			break;
		case SDL_MOUSEMOTION:
            _playerInput.lookDeltaX += static_cast<float>(event.motion.xrel);
            _playerInput.lookDeltaY += static_cast<float>(event.motion.yrel);
			break;
		default:
			//no-op
			break;
	}
}

void GameScene::handle_keystate(const Uint8* state)
{
    _playerInput.moveForward = state[SDL_SCANCODE_W];
    _playerInput.moveBackward = state[SDL_SCANCODE_S];
    _playerInput.moveLeft = state[SDL_SCANCODE_A];
    _playerInput.moveRight = state[SDL_SCANCODE_D];
}

void GameScene::clear_input()
{
    _playerInput = PlayerInputState{};
}

void GameScene::draw_imgui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	//ImGui::ShowDemoWindow();

	{
		draw_debug_map();
	}

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Render();
}

void GameScene::rebuild_pipelines()
{
	_camera->resize(_services.current_window_extent());
	build_pipelines();
}

void GameScene::build_pipelines()
{
	auto translate = PushConstant{
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	.size = sizeof(ObjectPushConstants),
	.build_constant = [](const RenderObject& obj) -> ObjectPushConstants
		{
			ObjectPushConstants push{};
			push.chunk_translate = obj.xzPos;
			return push;
		}
	};
	_services.materialManager->build_graphics_pipeline(
		{
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
		{ translate },
		{},
		"tri_mesh.vert.spv",
		"tri_mesh.frag.spv",
		"defaultmesh"
	);

	_services.materialManager->build_graphics_pipeline(
		{
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource),
            MaterialBinding::from_resource(2, 0, _fogResource)
        },
		{ translate },
		{ .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = true },
		"water_mesh.vert.spv",
		"water_mesh.frag.spv",
		"watermesh"
	);

    _services.materialManager->build_graphics_pipeline(
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = true },
        "glow_mesh.vert.spv",
        "glow_mesh.frag.spv",
        "glowmesh"
    );

    _services.materialManager->build_graphics_pipeline(
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = false, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary"
    );

	_services.materialManager->build_postprocess_pipeline(_fogResource);
	_services.materialManager->build_present_pipeline();
}

void GameScene::draw_debug_map()
{
    auto data_state_label = [](const DataState state) -> const char*
    {
        switch (state)
        {
        case DataState::Empty: return "E";
        case DataState::GenerateQueued: return "GQ";
        case DataState::Generating: return "GG";
        case DataState::Ready: return "R";
        case DataState::Dirty: return "D";
        }
        return "?";
    };

    auto mesh_state_label = [](const MeshState state) -> const char*
    {
        switch (state)
        {
        case MeshState::Missing: return "M";
        case MeshState::MeshQueued: return "MQ";
        case MeshState::Meshing: return "MS";
        case MeshState::MeshReady: return "MR";
        case MeshState::Uploaded: return "U";
        case MeshState::Stale: return "S";
        }
        return "?";
    };

    auto light_state_label = [](const LightState state) -> const char*
    {
        switch (state)
        {
        case LightState::Missing: return "M";
        case LightState::LightQueued: return "LQ";
        case LightState::Lighting: return "LG";
        case LightState::Ready: return "R";
        case LightState::Stale: return "S";
        }
        return "?";
    };

	ImGui::Begin("World Debug");
    if (ImGui::BeginTabBar("WorldDebugTabs"))
    {
        if (ImGui::BeginTabItem("Chunk Debug"))
        {
            const VkExtent2D windowExtent = _services.current_window_extent();
            ImGui::Text("Window size: %d x %d", windowExtent.width, windowExtent.height);

            const GameSnapshot& snapshot = _game.snapshot();
            ChunkCoord playerChunk = snapshot.currentChunk.value_or(World::get_chunk_coordinates(snapshot.player.position));
            if (snapshot.currentChunk.has_value())
            {
                ImGui::Text("Player Chunk: %d,%d", snapshot.currentChunk->x,  snapshot.currentChunk->z);
            }

            if (snapshot.hasCurrentBlock)
            {
                ImGui::Text("Camera World Position: x: %f, z: %f, y: %f", _camera->_position.x, _camera->_position.z, _camera->_position.y);
                ImGui::Text("Player World Position: x: %f, z: %f, y: %f", snapshot.player.position.x, snapshot.player.position.z, snapshot.player.position.y);
                ImGui::Text("Camera Front: x: %f, z: %f, y: %f", _camera->_front.x, _camera->_front.z, _camera->_front.y);
                auto local_pos = World::get_local_coordinates(snapshot.player.position);
                ImGui::Text("Player Local Position: x: %d, z: %d, y: %d", local_pos.x, local_pos.z, local_pos.y);
            }

            const int viewDistance = _settings.persistence().world.viewDistance;
            const int max_chunks = (viewDistance * 2) + 1;
            if (ImGui::BeginTable("MyGrid", max_chunks))
            {
                for (int row = 0; row < max_chunks; ++row)
                {
                    ImGui::TableNextRow();
                    for (int col = 0; col < max_chunks; ++col)
                    {
                        ImGui::TableSetColumnIndex(col);
                        const ChunkCoord chunkCoord = {playerChunk.x + (row - viewDistance), playerChunk.z + (col - viewDistance)};
                        const auto chunk = _game.get_chunk(chunkCoord);
                        const auto debugState = _game.chunk_manager().debug_state(chunkCoord);
                        if (chunk)
                        {
                            switch (chunk->_state.load(std::memory_order::acquire))
                            {
                            case ChunkState::Generated:
                                if (debugState.has_value() && debugState->meshState == MeshState::Stale)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 180, 0, 255));
                                }
                                else
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 255, 255));
                                }
                                break;
                            case ChunkState::Rendered:
                                if (chunk->_meshData->mesh->_isActive.load(std::memory_order::acquire) == true)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                                }
                                else
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 20, 125, 255));
                                }
                                break;
                            default:
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                                break;
                            }
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                        }
                        if (debugState.has_value())
                        {
                            ImGui::Text("[%d,%d g%u d%u l%u %s/%s/%s]", chunkCoord.x, chunkCoord.z, debugState->generationId, debugState->dataVersion, debugState->lightVersion, data_state_label(debugState->dataState), light_state_label(debugState->lightState), mesh_state_label(debugState->meshState));
                        }
                        else
                        {
                            ImGui::Text("[%d,%d,%d]", chunkCoord.x, chunkCoord.z, chunk != nullptr ? chunk->_gen.load(std::memory_order::acquire) : -1);
                        }
                        ImGui::PopStyleColor();
                    }
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings"))
        {
            const settings::GameSettingsPersistence& persistence = _settings.persistence();

            bool showChunkBoundaries = persistence.debug.showChunkBoundaries;
            if (ImGui::Checkbox("Show Chunk Boundaries (G)", &showChunkBoundaries))
            {
                _settings.mutate([showChunkBoundaries](settings::GameSettingsPersistence& updated)
                {
                    updated.debug.showChunkBoundaries = showChunkBoundaries;
                });
            }

            bool ambientOcclusionEnabled = persistence.world.ambientOcclusionEnabled;
            if (ImGui::Checkbox("Ambient Occlusion", &ambientOcclusionEnabled))
            {
                _settings.mutate([ambientOcclusionEnabled](settings::GameSettingsPersistence& updated)
                {
                    updated.world.ambientOcclusionEnabled = ambientOcclusionEnabled;
                });
            }

            ImGui::SliderInt("View Distance", &_viewDistanceDraft, 1, 20);
            const bool viewDistanceDirty = _viewDistanceDraft != persistence.world.viewDistance;
            if (!viewDistanceDirty)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Apply View Distance"))
            {
                const int requestedViewDistance = _viewDistanceDraft;
                _settings.mutate([requestedViewDistance](settings::GameSettingsPersistence& updated)
                {
                    updated.world.viewDistance = requestedViewDistance;
                });
            }
            if (!viewDistanceDirty)
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!viewDistanceDirty)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Reset View Distance"))
            {
                _viewDistanceDraft = persistence.world.viewDistance;
            }
            if (!viewDistanceDirty)
            {
                ImGui::EndDisabled();
            }

            bool pauseClock = persistence.dayNight.paused;
            if (ImGui::Checkbox("Pause Day/Night Clock", &pauseClock))
            {
                _settings.mutate([pauseClock](settings::GameSettingsPersistence& updated)
                {
                    updated.dayNight.paused = pauseClock;
                });
            }

            float timeOfDay = persistence.dayNight.timeOfDay;
            if (ImGui::SliderFloat("Time Of Day", &timeOfDay, 0.0f, 1.0f))
            {
                _settings.mutate([timeOfDay](settings::GameSettingsPersistence& updated)
                {
                    updated.dayNight.timeOfDay = timeOfDay;
                });
            }

            auto tuning = persistence.dayNight.tuning;
            bool tuningChanged = false;
            tuningChanged |= ImGui::SliderFloat("Cycle Seconds", &tuning.cycleDurationSeconds, 10.0f, 600.0f);
            tuningChanged |= ImGui::SliderFloat("Shadow Floor", &tuning.shadowFloor, 0.0f, 1.2f);
            tuningChanged |= ImGui::SliderFloat("Skylight Shadow Strength", &tuning.shadowStrength, 0.2f, 3.0f);
            tuningChanged |= ImGui::SliderFloat("Local Light Strength", &tuning.localLightStrength, 0.0f, 2.5f);
            tuningChanged |= ImGui::SliderFloat("Hemi Strength", &tuning.hemiStrength, 0.0f, 1.0f);
            tuningChanged |= ImGui::SliderFloat("Skylight Strength", &tuning.skylightStrength, 0.0f, 1.5f);
            tuningChanged |= ImGui::SliderFloat("AO Strength", &tuning.aoStrength, 0.0f, 0.5f);
            tuningChanged |= ImGui::SliderFloat("Water Fog Strength", &tuning.waterFogStrength, 0.0f, 1.0f);
            tuningChanged |= ImGui::ColorEdit3("Day Sky Zenith", &(tuning.daySkyZenith.x));
            tuningChanged |= ImGui::ColorEdit3("Day Sky Horizon", &(tuning.daySkyHorizon.x));
            tuningChanged |= ImGui::ColorEdit3("Day Ground", &(tuning.dayGround.x));
            tuningChanged |= ImGui::ColorEdit3("Day Sun", &(tuning.daySun.x));
            tuningChanged |= ImGui::ColorEdit3("Day Shadow", &(tuning.dayShadow.x));
            tuningChanged |= ImGui::ColorEdit3("Day Fog", &(tuning.dayFog.x));
            tuningChanged |= ImGui::ColorEdit3("Day Water Shallow", &(tuning.dayWaterShallow.x));
            tuningChanged |= ImGui::ColorEdit3("Day Water Deep", &(tuning.dayWaterDeep.x));
            tuningChanged |= ImGui::ColorEdit3("Dusk Horizon", &(tuning.duskSkyHorizon.x));
            tuningChanged |= ImGui::ColorEdit3("Dusk Fog", &(tuning.duskFog.x));
            tuningChanged |= ImGui::ColorEdit3("Night Sky Zenith", &(tuning.nightSkyZenith.x));
            tuningChanged |= ImGui::ColorEdit3("Night Sky Horizon", &(tuning.nightSkyHorizon.x));
            tuningChanged |= ImGui::ColorEdit3("Night Ground", &(tuning.nightGround.x));
            tuningChanged |= ImGui::ColorEdit3("Night Sun", &(tuning.nightSun.x));
            tuningChanged |= ImGui::ColorEdit3("Night Moon", &(tuning.nightMoon.x));
            tuningChanged |= ImGui::ColorEdit3("Night Shadow", &(tuning.nightShadow.x));
            tuningChanged |= ImGui::ColorEdit3("Night Fog", &(tuning.nightFog.x));
            tuningChanged |= ImGui::ColorEdit3("Night Water Shallow", &(tuning.nightWaterShallow.x));
            tuningChanged |= ImGui::ColorEdit3("Night Water Deep", &(tuning.nightWaterDeep.x));
            if (tuningChanged)
            {
                _settings.mutate([tuning](settings::GameSettingsPersistence& updated)
                {
                    updated.dayNight.tuning = tuning;
                });
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("World Gen"))
        {
            sync_world_gen_draft();
            TerrainGenerator& terrainGenerator = TerrainGenerator::instance();
            const TerrainGeneratorSettings appliedSettings = terrainGenerator.settings();
            const bool draftDirty = !equal_world_gen_settings(_worldGenDraft, appliedSettings);

            ImGui::Text("Stage terrain changes here. Nothing regenerates until you press the button.");
            ImGui::InputScalar("Seed", ImGuiDataType_U32, &_worldGenDraft.seed);
            ImGui::SeparatorText("Shape");
            ImGui::SliderFloat("Terrain Frequency", &_worldGenDraft.shape.terrainFrequency, 0.00005f, 0.0050f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Climate Frequency", &_worldGenDraft.shape.climateFrequency, 0.00005f, 0.0050f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("River Frequency", &_worldGenDraft.shape.riverFrequency, 0.00005f, 0.0100f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("River Width Threshold", &_worldGenDraft.shape.riverThreshold, 0.005f, 0.35f, "%.3f");
            ImGui::SliderFloat("Erosion Suppression Low", &_worldGenDraft.shape.erosionSuppressionLow, 0.1f, 2.5f, "%.2f");
            ImGui::SliderFloat("Erosion Suppression High", &_worldGenDraft.shape.erosionSuppressionHigh, 0.1f, 2.5f, "%.2f");

            ImGui::SeparatorText("Biome Thresholds");
            ImGui::SliderFloat("Ocean Continentalness", &_worldGenDraft.biome.oceanContinentalnessThreshold, -1.0f, 0.2f, "%.2f");
            ImGui::SliderFloat("River Biome Threshold", &_worldGenDraft.biome.riverBlendThreshold, 0.05f, 0.95f, "%.2f");
            ImGui::SliderInt("River Bank Min Offset", &_worldGenDraft.biome.riverMinBankHeightOffset, -16, 16);
            ImGui::SliderInt("Beach Min Offset", &_worldGenDraft.biome.beachMinHeightOffset, -12, 8);
            ImGui::SliderInt("Beach Max Offset", &_worldGenDraft.biome.beachMaxHeightOffset, -6, 20);
            ImGui::SliderInt("Mountain Height Offset", &_worldGenDraft.biome.mountainHeightOffset, 8, 96);
            ImGui::SliderFloat("Mountain Peaks Threshold", &_worldGenDraft.biome.mountainPeaksThreshold, -0.2f, 1.0f, "%.2f");
            ImGui::SliderFloat("Forest Humidity Threshold", &_worldGenDraft.biome.forestHumidityThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Forest Temperature Threshold", &_worldGenDraft.biome.forestTemperatureThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderInt("Mountain Stone Line", &_worldGenDraft.biome.mountainStoneHeightOffset, 20, 120);

            ImGui::SeparatorText("Surface");
            ImGui::SliderInt("River Target Offset", &_worldGenDraft.surface.riverTargetHeightOffset, -16, 8);
            ImGui::SliderInt("River Min Depth", &_worldGenDraft.surface.riverMinDepth, 1, 8);
            ImGui::SliderInt("River Max Depth", &_worldGenDraft.surface.riverMaxDepth, 1, 12);
            ImGui::SliderInt("Ocean Floor Offset", &_worldGenDraft.surface.oceanFloorHeightOffset, -24, 4);
            ImGui::SliderInt("Shore Min Offset", &_worldGenDraft.surface.shoreMinHeightOffset, -8, 6);
            ImGui::SliderInt("Shore Max Offset", &_worldGenDraft.surface.shoreMaxHeightOffset, -2, 10);
            ImGui::SliderInt("River Stone Depth", &_worldGenDraft.surface.riverStoneDepth, 1, 8);
            ImGui::SliderInt("Ocean Stone Depth", &_worldGenDraft.surface.oceanStoneDepth, 1, 8);
            ImGui::SliderInt("Shore Stone Depth", &_worldGenDraft.surface.shoreStoneDepth, 1, 8);
            ImGui::SliderInt("Plains Stone Depth", &_worldGenDraft.surface.plainsStoneDepth, 1, 12);
            ImGui::SliderInt("Mountain Stone Depth", &_worldGenDraft.surface.mountainStoneDepth, 1, 8);

            draw_spline_editor("ErosionSplineTable", "Erosion Spline", _worldGenDraft.erosionSplines);
            draw_spline_editor("PeakSplineTable", "Peak Spline", _worldGenDraft.peakSplines);
            draw_spline_editor("ContinentalSplineTable", "Continentalness Spline", _worldGenDraft.continentalSplines);

            ImGui::SeparatorText("Current Area Preview");
            const char* layerLabels[] = {
                "Final Height",
                "Continentalness",
                "Erosion",
                "Peaks/Valleys",
                "Temperature",
                "Humidity",
                "River",
                "Biome"
            };
            ImGui::Combo("Preview Layer", &_worldGenPreviewLayer, layerLabels, IM_ARRAYSIZE(layerLabels));

            const ChunkCoord centerChunk = _game.snapshot().currentChunk.value_or(World::get_chunk_coordinates(_game.snapshot().player.position));
            const int previewViewDistance = _settings.persistence().world.viewDistance;
            ImGui::Text("Preview Center Chunk: %d, %d", centerChunk.x, centerChunk.z);
            ImGui::Text("Preview Area: %dx%d chunks", (previewViewDistance * 2) + 1, (previewViewDistance * 2) + 1);
            draw_noise_preview("WorldGenPreview", centerChunk, previewViewDistance, _worldGenPreviewLayer);

            if (!draftDirty)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Regenerate World With Draft Settings"))
            {
                terrainGenerator.apply_settings(_worldGenDraft);
                _worldGenDraft = terrainGenerator.settings();
                _game.regenerate_world();
            }
            if (!draftDirty)
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Draft To File"))
            {
                if (_services.configService != nullptr)
                {
                    _services.configService->world_gen().save(_worldGenDraft);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Applied Settings"))
            {
                _worldGenDraft = appliedSettings;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Saved File"))
            {
                if (_services.configService != nullptr)
                {
                    _worldGenDraft = _services.configService->world_gen().load_or_default();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset To Defaults"))
            {
                _worldGenDraft = TerrainGenerator::default_settings();
            }

            if (draftDirty)
            {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "Draft differs from active worldgen settings.");
            }
            else
            {
                ImGui::TextDisabled("Draft matches active worldgen settings.");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
	ImGui::End();
}

void GameScene::update_fog_ubo() const
{
	FogUBO fogUBO{};
    const settings::GameSettingsPersistence& persistence = _settings.persistence();
    const settings::LightingTuningSettings& tuning = persistence.dayNight.tuning;
    const float angle = persistence.dayNight.timeOfDay * glm::two_pi<float>();
    const float sunHeight = std::sin(angle);
    const float dayFactor = std::clamp((sunHeight + 0.2f) / 1.1f, 0.0f, 1.0f);
    const glm::vec3& dayFog = tuning.dayFog;
    const glm::vec3& duskFog = tuning.duskFog;
    const glm::vec3& nightFog = tuning.nightFog;
	fogUBO.fogColor = glm::mix(glm::mix(nightFog, duskFog, std::clamp(1.0f - std::abs(sunHeight), 0.0f, 1.0f)), dayFog, dayFactor);
	fogUBO.fogEndColor = glm::mix(fogUBO.fogColor * 0.82f, fogUBO.fogColor * 1.12f, dayFactor);

	fogUBO.fogCenter = _game.snapshot().player.position;
	fogUBO.fogRadius = _settings.view_distance_runtime_settings().fogRadius;
	const VkExtent2D windowExtent = _services.current_window_extent();
	fogUBO.screenSize = glm::ivec2(windowExtent.width, windowExtent.height);
	fogUBO.invViewProject = glm::inverse(_camera->_projection * _camera->_view);

	void* data;
	vmaMapMemory(_services.allocator, _fogResource->value.buffer._allocation, &data);
	memcpy(data, &fogUBO, sizeof(FogUBO));
	vmaUnmapMemory(_services.allocator, _fogResource->value.buffer._allocation);
}

void GameScene::update_lighting_ubo() const
{
    const settings::GameSettingsPersistence& persistence = _settings.persistence();
    const settings::LightingTuningSettings& tuning = persistence.dayNight.tuning;
    const float angle = persistence.dayNight.timeOfDay * glm::two_pi<float>();
    const float sunHeight = std::sin(angle);
    const float dayFactor = std::clamp((sunHeight + 0.2f) / 1.1f, 0.0f, 1.0f);
    const float duskFactor = std::clamp(1.0f - std::abs(sunHeight), 0.0f, 1.0f) * (1.0f - dayFactor);

    LightingUBO lighting{};
    lighting.skyZenithColor = glm::vec4(glm::mix(tuning.nightSkyZenith, tuning.daySkyZenith, dayFactor), 1.0f);
    lighting.skyHorizonColor = glm::vec4(glm::mix(tuning.nightSkyHorizon, glm::mix(tuning.duskSkyHorizon, tuning.daySkyHorizon, dayFactor), std::max(dayFactor, duskFactor)), 1.0f);
    lighting.groundColor = glm::vec4(glm::mix(tuning.nightGround, tuning.dayGround, dayFactor), 1.0f);
    lighting.sunColor = glm::vec4(glm::mix(tuning.nightSun, tuning.daySun, std::max(dayFactor, duskFactor)), 1.0f);
    lighting.moonColor = glm::vec4(tuning.nightMoon, 1.0f);
    lighting.shadowColor = glm::vec4(glm::mix(tuning.nightShadow, tuning.dayShadow, dayFactor), 1.0f);
    lighting.waterShallowColor = glm::vec4(glm::mix(tuning.nightWaterShallow, tuning.dayWaterShallow, dayFactor), 1.0f);
    lighting.waterDeepColor = glm::vec4(glm::mix(tuning.nightWaterDeep, tuning.dayWaterDeep, dayFactor), 1.0f);
    lighting.params1 = glm::vec4(persistence.dayNight.timeOfDay, sunHeight, dayFactor, persistence.world.ambientOcclusionEnabled ? tuning.aoStrength : 0.0f);
    lighting.params2 = glm::vec4(tuning.shadowFloor, tuning.hemiStrength, tuning.skylightStrength, tuning.waterFogStrength);
    lighting.params3 = glm::vec4(tuning.shadowStrength, tuning.localLightStrength, 0.0f, 0.0f);

    void* data;
    vmaMapMemory(_services.allocator, _lightingResource->value.buffer._allocation, &data);
    memcpy(data, &lighting, sizeof(LightingUBO));
    vmaUnmapMemory(_services.allocator, _lightingResource->value.buffer._allocation);
}

void GameScene::update_uniform_buffer() const
{
	CameraUBO cameraUBO{};
	cameraUBO.projection = _camera->_projection;
	cameraUBO.view = _camera->_view;
	cameraUBO.viewproject = _camera->_projection * _camera->_view;

	void* data;
	vmaMapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation);
}
void GameScene::create_camera()
{
	_camera = std::make_unique<Camera>(GameConfig::DEFAULT_POSITION, _services.current_window_extent());
}

void GameScene::sync_camera_to_game(const float deltaTime)
{
    const PlayerSnapshot& player = _game.snapshot().player;
    _camera->_position = player.position;
    _camera->_front = player.front;
    _camera->_up = player.up;
    _camera->_yaw = player.yaw;
    _camera->_pitch = player.pitch;
    _camera->update(deltaTime);
}

void GameScene::sync_target_block()
{
    _targetBlock = _game.raycast_target_block(GameConfig::BLOCK_INTERACTION_DISTANCE);
}

void GameScene::sync_target_block_outline()
{
    if (!_targetBlock.has_value())
    {
        clear_target_block_outline();
        _outlinedBlockWorldPos.reset();
        return;
    }

    const glm::ivec3 worldPos = _targetBlock->_worldPos;
    if (_outlinedBlockWorldPos.has_value() && _outlinedBlockWorldPos.value() == worldPos)
    {
        return;
    }

    clear_target_block_outline();
    const glm::ivec2 chunkOrigin = World::get_chunk_origin(glm::vec3(worldPos));
    const glm::ivec3 localPos = World::get_local_coordinates(glm::vec3(worldPos));

    if (_targetBlockOutlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_targetBlockOutlineMesh));
    }

    _targetBlockOutlineMesh = Mesh::create_block_outline_mesh(glm::vec3(localPos));
    _services.meshManager->UploadQueue.enqueue(_targetBlockOutlineMesh);
    _outlinedBlockWorldPos = worldPos;

    _targetBlockOutlineHandle = _renderState.opaqueObjects.insert(RenderObject{
        .mesh = _targetBlockOutlineMesh,
        .material = _services.materialManager->get_material("chunkboundary"),
        .xzPos = chunkOrigin,
        .layer = RenderLayer::Opaque
    });
}

void GameScene::sync_chunk_boundary_debug()
{
    clear_chunk_boundary_debug();
    if (!_settings.persistence().debug.showChunkBoundaries)
    {
        return;
    }

    if (_chunkBoundaryMesh == nullptr)
    {
        _chunkBoundaryMesh = Mesh::create_chunk_boundary_mesh();
        _services.meshManager->UploadQueue.enqueue(_chunkBoundaryMesh);
    }

    const auto debugMaterial = _services.materialManager->get_material("chunkboundary");
    const GameSnapshot& snapshot = _game.snapshot();
    const ChunkCoord playerChunk = snapshot.currentChunk.value_or(World::get_chunk_coordinates(snapshot.player.position));
    const int viewDistance = _settings.persistence().world.viewDistance;
    _chunkBoundaryHandles.reserve(static_cast<size_t>((viewDistance * 2 + 1) * (viewDistance * 2 + 1)));

    for (int chunkZ = playerChunk.z - viewDistance; chunkZ <= playerChunk.z + viewDistance; ++chunkZ)
    {
        for (int chunkX = playerChunk.x - viewDistance; chunkX <= playerChunk.x + viewDistance; ++chunkX)
        {
            const ChunkCoord chunkCoord{ chunkX, chunkZ };
            const Chunk* const chunk = _game.get_chunk(chunkCoord);
            if (chunk == nullptr)
            {
                continue;
            }

            _chunkBoundaryHandles.push_back(_renderState.opaqueObjects.insert(RenderObject{
            .mesh = _chunkBoundaryMesh,
            .material = debugMaterial,
            .xzPos = glm::ivec2(chunk->_data->position.x, chunk->_data->position.y),
            .layer = RenderLayer::Opaque
        }));
        }
    }
}

void GameScene::clear_chunk_boundary_debug()
{
    for (const auto handle : _chunkBoundaryHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }

    _chunkBoundaryHandles.clear();
}

void GameScene::clear_target_block_outline()
{
    if (_targetBlockOutlineHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_targetBlockOutlineHandle.value());
        _targetBlockOutlineHandle.reset();
    }
}

void GameScene::bind_settings()
{
    _settings.bind_view_distance_handler([this](const settings::ViewDistanceRuntimeSettings& settings)
    {
        apply_view_distance_settings(settings);
    });
    _settings.bind_ambient_occlusion_handler([this](const settings::AmbientOcclusionRuntimeSettings& settings)
    {
        apply_ambient_occlusion_settings(settings);
    });
}

void GameScene::apply_view_distance_settings(const settings::ViewDistanceRuntimeSettings& settings)
{
    _viewDistanceDraft = settings.viewDistance;
    _chunkRenderRegistry.clear(_renderState);
    clear_chunk_boundary_debug();
    clear_target_block_outline();
    _outlinedBlockWorldPos.reset();
    if (_chunkBoundaryMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_chunkBoundaryMesh));
    }
    if (_targetBlockOutlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_targetBlockOutlineMesh));
    }

    _services.meshManager->apply_view_distance_settings(settings);
    _game.chunk_manager().apply_streaming_settings(ChunkStreamingSettings{
        .viewDistance = settings.viewDistance
    });
}

void GameScene::apply_ambient_occlusion_settings(const settings::AmbientOcclusionRuntimeSettings& settings)
{
    _game.chunk_manager().apply_mesh_settings(ChunkMeshSettings{
        .ambientOcclusionEnabled = settings.enabled
    });
}

void GameScene::sync_world_gen_draft()
{
    if (_worldGenDraftInitialized)
    {
        return;
    }

    _worldGenDraft = TerrainGenerator::instance().settings();
    _worldGenDraftInitialized = true;
}
