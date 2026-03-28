#include "game_scene.h"
#include "vk_engine.h"
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <ranges>
#include <unordered_set>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "orbit_orientation_gizmo.h"
#include "render/mesh_release_queue.h"
#include "components/voxel_model_component.h"
#include "string_utils.h"
#include "voxel/voxel_component_render_adapter.h"
#include "voxel/voxel_model_component_adapter.h"

namespace
{
    constexpr std::string_view GameSceneMaterialScope = "game";

    bool equal_aabb(const AABB& lhs, const AABB& rhs) noexcept
    {
        return lhs.min.x == rhs.min.x &&
            lhs.min.y == rhs.min.y &&
            lhs.min.z == rhs.min.z &&
            lhs.max.x == rhs.max.x &&
            lhs.max.y == rhs.max.y &&
            lhs.max.z == rhs.max.z;
    }

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
            lhs.shape.continentalFrequency == rhs.shape.continentalFrequency &&
            lhs.shape.erosionFrequency == rhs.shape.erosionFrequency &&
            lhs.shape.peaksFrequency == rhs.shape.peaksFrequency &&
            lhs.shape.detailFrequency == rhs.shape.detailFrequency &&
            lhs.shape.seaLevel == rhs.shape.seaLevel &&
            lhs.shape.riversEnabled == rhs.shape.riversEnabled &&
            lhs.shape.riverFrequency == rhs.shape.riverFrequency &&
            lhs.shape.riverThreshold == rhs.shape.riverThreshold &&
            lhs.shape.continentalStrength == rhs.shape.continentalStrength &&
            lhs.shape.peaksStrength == rhs.shape.peaksStrength &&
            lhs.shape.erosionStrength == rhs.shape.erosionStrength &&
            lhs.shape.valleyStrength == rhs.shape.valleyStrength &&
            lhs.shape.detailStrength == rhs.shape.detailStrength &&
            lhs.shape.erosionSuppressionLow == rhs.shape.erosionSuppressionLow &&
            lhs.shape.erosionSuppressionHigh == rhs.shape.erosionSuppressionHigh &&
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

    const char* biome_label(const BiomeType biome)
    {
        switch (biome)
        {
        case BiomeType::None:
            return "None";
        case BiomeType::Ocean:
            return "Ocean";
        case BiomeType::Shore:
            return "Shore";
        case BiomeType::Plains:
            return "Plains";
        case BiomeType::Forest:
            return "Forest";
        case BiomeType::River:
            return "River";
        case BiomeType::Mountains:
            return "Mountains";
        default:
            return "Unknown";
        }
    }

    ImU32 noise_preview_color(const TerrainColumnSample& column, const int layer)
    {
        auto biome_color = [](const BiomeType biome) -> ImU32
        {
            switch (biome)
            {
            case BiomeType::None:
                return IM_COL32(96, 96, 96, 255);
            case BiomeType::Ocean:
                return IM_COL32(58, 104, 184, 255);
            case BiomeType::Shore:
                return IM_COL32(214, 195, 132, 255);
            case BiomeType::Plains:
                return IM_COL32(122, 176, 92, 255);
            case BiomeType::Forest:
                return IM_COL32(58, 122, 66, 255);
            case BiomeType::River:
                return IM_COL32(92, 152, 214, 255);
            case BiomeType::Mountains:
                return IM_COL32(150, 150, 162, 255);
            default:
                return IM_COL32(255, 255, 255, 255);
            }
        };

        switch (layer)
        {
        case 0:
            return pack_color(gradient_color(static_cast<float>(column.surfaceHeight) / static_cast<float>(CHUNK_HEIGHT - 1)));
        case 1:
            return pack_color(gradient_color(static_cast<float>(column.baseSurfaceHeight) / static_cast<float>(CHUNK_HEIGHT - 1)));
        case 2:
            return pack_color(gradient_color((column.noise.continentalness + 1.0f) * 0.5f));
        case 3:
            return pack_color(gradient_color((column.noise.erosion + 1.0f) * 0.5f));
        case 4:
            return pack_color(gradient_color((column.noise.peaksValleys + 1.0f) * 0.5f));
        case 5:
            return pack_color(gradient_color((column.noise.detail + 1.0f) * 0.5f));
        case 6:
            return pack_color(gradient_color((column.noise.river + 1.0f) * 0.5f));
        case 7:
            return biome_color(column.biome);
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
            ImGui::Text("Base Height: %d", column.baseSurfaceHeight);
            ImGui::Text("Cont: %.2f", column.noise.continentalness);
            ImGui::Text("Erosion: %.2f", column.noise.erosion);
            ImGui::Text("Peaks: %.2f", column.noise.peaksValleys);
            ImGui::Text("Detail: %.2f", column.noise.detail);
            ImGui::Text("River: %.2f", column.noise.river);
            ImGui::Text("Biome: %s", biome_label(column.biome));
            ImGui::Text("River Column: %s", column.hasRiver ? "Yes" : "No");
            ImGui::EndTooltip();
        }
    }
}


GameScene::GameScene(const SceneServices& services):
    _services(services)
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
        _settings = settings::SettingsManager(_services.configService->game_settings().load_or_default());
    }
    if (_services.configService != nullptr)
    {
        TerrainGenerator::instance().apply_settings(_services.configService->world_gen().load_or_default());
    }
    bind_settings();
    sync_world_gen_draft();
    refresh_player_assembly_assets();
    sync_camera_to_game(0.0f);
    _worldLightSampler = std::make_unique<world_lighting::WorldLightSampler>(_game.chunk_manager(), _dynamicLightRegistry);

    const auto preferredPlayerAssemblyIt = std::ranges::find_if(_savedPlayerAssemblyIds, [](const std::string& assetId)
    {
        return assetId.starts_with("player");
    });
    if (preferredPlayerAssemblyIt != _savedPlayerAssemblyIds.end())
    {
        _playerAssemblyAssetId = *preferredPlayerAssemblyIt;
        _selectedPlayerAssemblyIndex = static_cast<int>(std::distance(_savedPlayerAssemblyIds.begin(), preferredPlayerAssemblyIt));
        _game.set_player_render_assembly_asset_id(_playerAssemblyAssetId);
        _playerAssemblyStatus = std::format("Player assembly '{}' selected.", _playerAssemblyAssetId);
    }

	std::println("GameScene created!");
}

GameScene::~GameScene()
{
    clear_target_block_outline();
    clear_spatial_collider_debug();
    release_spatial_collider_debug_meshes();
    clear_chunk_boundary_debug();
    _chunkDecorationRenderRegistry.clear(_renderState);
    _playerVoxelRenderRegistry.clear(_renderState);
    _voxelRenderRegistry.clear(_renderState);
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
	ZoneScopedN("GameScene::UpdateBuffers");
    sync_runtime_lights();
    sync_player_render_instance();
	{
		ZoneScopedN("GameScene::SyncChunkRenderRegistry");
		_chunkRenderRegistry.sync(
			_game.chunk_manager(),
			*_services.meshManager,
			*_services.materialManager,
            GameSceneMaterialScope,
			_renderState);
	}
    const ChunkCoord centerChunk = _game.snapshot().currentChunk.value_or(World::get_chunk_coordinates(_game.snapshot().player.position));
    {
        ZoneScopedN("GameScene::SyncDecorationRegistry");
        _chunkDecorationRenderRegistry.sync(
            _game.chunk_manager(),
            centerChunk,
            _settings.persistence().world.viewDistance,
            _game.voxel_asset_manager(),
            *_services.meshManager,
            *_services.materialManager,
            GameSceneMaterialScope,
            _worldLightSampler.get(),
            _renderState);
    }
    {
        ZoneScopedN("GameScene::SyncPlayerVoxelRegistry");
        _playerVoxelRenderRegistry.sync(*_services.meshManager, *_services.materialManager, GameSceneMaterialScope, _renderState, _worldLightSampler.get());
    }
    {
        ZoneScopedN("GameScene::SyncVoxelRegistry");
        _voxelRenderRegistry.sync(*_services.meshManager, *_services.materialManager, GameSceneMaterialScope, _renderState, _worldLightSampler.get());
    }
    sync_target_block_outline();
    sync_spatial_collider_debug();
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
	ZoneScopedN("GameScene::Update");
	_game.set_player_input(_playerInput);
    _playerInput.jumpPressed = false;
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
    if (!_runtimeVoxelDemoInitialized || _runtimeVoxelDemoDirty)
    {
        rebuild_runtime_voxel_demo();
    }
}

void GameScene::handle_input(const SDL_Event& event)
{
	ZoneScopedN("GameScene::HandleInput");
	switch(event.type) {
		case SDL_KEYDOWN:
            if (!event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_G)
            {
                _settings.mutate([](settings::GameSettingsPersistence& persistence)
                {
                    persistence.debug.showChunkBoundaries = !persistence.debug.showChunkBoundaries;
                });
            }
            else if (!event.key.repeat &&
                event.key.keysym.scancode == SDL_SCANCODE_SPACE &&
                !_settings.persistence().player.flyModeEnabled)
            {
                _playerInput.jumpPressed = true;
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
    ZoneScopedN("GameScene::HandleKeystate");
    _playerInput.moveForward = state[SDL_SCANCODE_W];
    _playerInput.moveBackward = state[SDL_SCANCODE_S];
    _playerInput.moveLeft = state[SDL_SCANCODE_A];
    _playerInput.moveRight = state[SDL_SCANCODE_D];

    const bool flyModeEnabled = _settings.persistence().player.flyModeEnabled;
    _playerInput.moveUp = flyModeEnabled && state[SDL_SCANCODE_SPACE];
    _playerInput.moveDown = flyModeEnabled && (state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_C]);
}

void GameScene::clear_input()
{
    ZoneScopedN("GameScene::ClearInput");
    _playerInput = PlayerInputState{};
}

void GameScene::draw_imgui()
{
	ZoneScopedN("GameScene::DrawImGui");
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	//ImGui::ShowDemoWindow();

	{
		draw_debug_map();
	}

    draw_camera_orientation_gizmo();

	ImGui::Render();
}

void GameScene::draw_camera_orientation_gizmo() const
{
    glm::mat4 gizmoView = _camera->_view;
    (void)draw_orbit_orientation_gizmo(gizmoView, 4.0f);
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
			push.modelMatrix = obj.transform;
            push.sampledLocalLightAndSunlight = glm::vec4(obj.sampledLight.localLight, obj.sampledLight.sunlight);
            push.sampledDynamicLightAndMode = glm::vec4(obj.sampledLight.dynamicLight, static_cast<float>(obj.lightingMode));
			return push;
		}
	};
	_services.materialManager->build_graphics_pipeline(
        GameSceneMaterialScope,
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
        GameSceneMaterialScope,
		{
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource),
            MaterialBinding::from_resource(2, 0, _fogResource)
        },
		{ translate },
		{ .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha },
		"water_mesh.vert.spv",
		"water_mesh.frag.spv",
		"watermesh"
	);

    _services.materialManager->build_graphics_pipeline(
        GameSceneMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Additive },
        "glow_mesh.vert.spv",
        "glow_mesh.frag.spv",
        "glowmesh"
    );

    _services.materialManager->build_graphics_pipeline(
        GameSceneMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary"
    );

	_services.materialManager->build_postprocess_pipeline(GameSceneMaterialScope, _fogResource);
	_services.materialManager->build_present_pipeline(GameSceneMaterialScope);
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
    const std::vector<DebugSpatialColliderSnapshot> colliderSnapshots = _game.debug_spatial_colliders();
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

        if (ImGui::BeginTabItem("Voxel Props"))
        {
            char assetIdBuffer[128]{};
            copy_cstr_truncating(assetIdBuffer, _runtimeVoxelAssetId);
            if (ImGui::InputText("Asset Id", assetIdBuffer, IM_ARRAYSIZE(assetIdBuffer)))
            {
                _runtimeVoxelAssetId = assetIdBuffer;
            }

            ImGui::TextWrapped("Loads a saved voxel asset and spawns a small shared-mesh prop cluster on terrain near the player.");
            ImGui::Text("Repository Path: %s", _game.voxel_repository().resolve_path(_runtimeVoxelAssetId).string().c_str());
            ImGui::Text("Loaded Assets: %llu", static_cast<unsigned long long>(_game.voxel_asset_manager().loaded_asset_count()));
            ImGui::Text("Active Instances: %llu", static_cast<unsigned long long>(_voxelRenderRegistry.instance_count()));
            ImGui::Text("Chunk Decoration Chunks: %llu", static_cast<unsigned long long>(_chunkDecorationRenderRegistry.active_chunk_count()));
            ImGui::Text("Chunk Decoration Instances: %llu", static_cast<unsigned long long>(_chunkDecorationRenderRegistry.active_instance_count()));
            ImGui::Text("Dynamic Lights: %llu", static_cast<unsigned long long>(_dynamicLightRegistry.active_light_count()));
            ImGui::Checkbox("Player Torch Light", &_playerTorchLightEnabled);
            ImGui::SliderFloat("Torch Radius", &_playerTorchRadius, 1.0f, 18.0f, "%.1f");
            ImGui::SliderFloat("Torch Intensity", &_playerTorchIntensity, 0.1f, 4.0f, "%.2f");
            ImGui::ColorEdit3("Torch Color", &_playerTorchColor.x);

            if (ImGui::Button("Load Demo Props"))
            {
                _runtimeVoxelDemoDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Demo Props"))
            {
                _voxelRenderRegistry.clear(_renderState);
                _runtimeVoxelDemoInitialized = true;
                _runtimeVoxelDemoDirty = false;
                _runtimeVoxelStatus = "Cleared runtime voxel prop demo instances.";
            }

            ImGui::Separator();
            ImGui::TextWrapped("%s", _runtimeVoxelStatus.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings"))
        {
            const settings::GameSettingsPersistence& persistence = _settings.persistence();
            ImGui::Text("Settings changes apply live. File persistence only happens when you press Save.");

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

            if (_services.configService == nullptr)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Save Settings To File"))
            {
                _services.configService->game_settings().save(_settings.persistence());
            }
            if (_services.configService == nullptr)
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (_services.configService == nullptr)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Reload Saved Settings"))
            {
                _settings = settings::SettingsManager(_services.configService->game_settings().load_or_default());
                bind_settings();
            }
            if (_services.configService == nullptr)
            {
                ImGui::EndDisabled();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("PlayerSettings"))
        {
            const settings::GameSettingsPersistence& persistence = _settings.persistence();
            ImGui::Text("Player tuning applies live. Save from Settings when you want it persisted.");

            bool flyModeEnabled = persistence.player.flyModeEnabled;
            if (ImGui::Checkbox("Fly Mode", &flyModeEnabled))
            {
                _settings.mutate([flyModeEnabled](settings::GameSettingsPersistence& updated)
                {
                    updated.player.flyModeEnabled = flyModeEnabled;
                });
                _playerInput.jumpPressed = false;
            }

            ImGui::TextWrapped("Fly mode uses WASD for camera-relative movement, Space to rise, and Left Ctrl or C to descend.");

            ImGui::SeparatorText("Player Render");
            ImGui::TextWrapped("Player render is assembly-driven. Select a saved assembly asset to represent the player at runtime.");

            char playerAssemblyBuffer[128]{};
            copy_cstr_truncating(playerAssemblyBuffer, _playerAssemblyAssetId);
            if (ImGui::InputText("Assembly Asset Id", playerAssemblyBuffer, IM_ARRAYSIZE(playerAssemblyBuffer)))
            {
                _playerAssemblyAssetId = playerAssemblyBuffer;
                refresh_player_assembly_assets();
            }

            if (ImGui::Button("Apply Assembly"))
            {
                _game.set_player_render_assembly_asset_id(_playerAssemblyAssetId);
                _playerAssemblyStatus = _playerAssemblyAssetId.empty()
                    ? "Cleared player assembly selection."
                    : std::format("Player assembly '{}' queued for runtime render.", _playerAssemblyAssetId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh Assemblies"))
            {
                refresh_player_assembly_assets();
            }

            ImGui::Text("%d saved assembly file(s)", static_cast<int>(_savedPlayerAssemblyIds.size()));
            const float playerAssemblyListHeight = std::min(
                180.0f,
                28.0f + (static_cast<float>(_savedPlayerAssemblyIds.size()) * ImGui::GetTextLineHeightWithSpacing()));
            if (ImGui::BeginChild("PlayerAssemblyAssetList", ImVec2(0.0f, playerAssemblyListHeight), true))
            {
                if (_savedPlayerAssemblyIds.empty())
                {
                    ImGui::TextDisabled("No saved .vxma assets found in %s", _game.voxel_assembly_repository().root_path().string().c_str());
                }
                else
                {
                    for (int index = 0; index < static_cast<int>(_savedPlayerAssemblyIds.size()); ++index)
                    {
                        const bool selected = index == _selectedPlayerAssemblyIndex;
                        if (ImGui::Selectable(_savedPlayerAssemblyIds[static_cast<size_t>(index)].c_str(), selected))
                        {
                            _selectedPlayerAssemblyIndex = index;
                            _playerAssemblyAssetId = _savedPlayerAssemblyIds[static_cast<size_t>(index)];
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            _playerAssemblyAssetId = _savedPlayerAssemblyIds[static_cast<size_t>(index)];
                            _game.set_player_render_assembly_asset_id(_playerAssemblyAssetId);
                            _playerAssemblyStatus = std::format("Player assembly '{}' queued for runtime render.", _playerAssemblyAssetId);
                        }
                    }
                }
            }
            ImGui::EndChild();

            if (_selectedPlayerAssemblyIndex < 0 || _selectedPlayerAssemblyIndex >= static_cast<int>(_savedPlayerAssemblyIds.size()))
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Apply Selected Assembly"))
            {
                _playerAssemblyAssetId = _savedPlayerAssemblyIds[static_cast<size_t>(_selectedPlayerAssemblyIndex)];
                _game.set_player_render_assembly_asset_id(_playerAssemblyAssetId);
                _playerAssemblyStatus = std::format("Player assembly '{}' queued for runtime render.", _playerAssemblyAssetId);
            }
            if (_selectedPlayerAssemblyIndex < 0 || _selectedPlayerAssemblyIndex >= static_cast<int>(_savedPlayerAssemblyIds.size()))
            {
                ImGui::EndDisabled();
            }

            ImGui::TextWrapped("%s", _playerAssemblyStatus.c_str());
            ImGui::TextWrapped("Player world collision now comes from the active assembly's authored collision settings. Camera Target Offset remains a live gameplay tuning value.");
            ImGui::TextWrapped("Live collider inspection and world-space collider overlays are in the Spatial Debug tab.");

            auto playerSettings = persistence.player;
            bool playerSettingsChanged = false;
            playerSettingsChanged |= ImGui::SliderFloat("Move Speed", &playerSettings.moveSpeed, 0.0f, 60.0f, "%.2f");
            playerSettingsChanged |= ImGui::SliderFloat("Air Control", &playerSettings.airControl, 0.0f, 1.0f, "%.2f");
            playerSettingsChanged |= ImGui::SliderFloat("Gravity", &playerSettings.gravity, 0.0f, 60.0f, "%.2f");
            playerSettingsChanged |= ImGui::SliderFloat("Jump Velocity", &playerSettings.jumpVelocity, 0.0f, 20.0f, "%.2f");
            playerSettingsChanged |= ImGui::SliderFloat("Max Fall Speed", &playerSettings.maxFallSpeed, 0.0f, 80.0f, "%.2f");
            ImGui::SeparatorText("Camera");
            playerSettingsChanged |= ImGui::DragFloat3("Camera Target Offset", &playerSettings.cameraTargetOffset.x, 0.01f, -10.0f, 10.0f, "%.2f");
            if (playerSettingsChanged)
            {
                _settings.mutate([playerSettings](settings::GameSettingsPersistence& updated)
                {
                    updated.player = playerSettings;
                });
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Spatial Debug"))
        {
            if (ImGui::Checkbox("Show Spatial Collider Bounds In World", &_showSpatialColliderBounds))
            {
                if (!_showSpatialColliderBounds)
                {
                    clear_spatial_collider_debug();
                    release_spatial_collider_debug_meshes();
                }
            }

            ImGui::Text("%d runtime collider snapshot(s)", static_cast<int>(colliderSnapshots.size()));
            if (colliderSnapshots.empty())
            {
                ImGui::TextDisabled("No spatial colliders are currently exposed by the runtime.");
            }
            else
            {
                for (size_t index = 0; index < colliderSnapshots.size(); ++index)
                {
                    const DebugSpatialColliderSnapshot& snapshot = colliderSnapshots[index];
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::CollapsingHeader(snapshot.label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Text("Id: %s", snapshot.id.c_str());
                        ImGui::Text("Valid: %s", snapshot.valid ? "yes" : "no");
                        ImGui::Text(
                            "Origin: %.2f, %.2f, %.2f",
                            snapshot.origin.x,
                            snapshot.origin.y,
                            snapshot.origin.z);
                        ImGui::Text(
                            "Local Bounds Min: %.2f, %.2f, %.2f",
                            snapshot.localBounds.min.x,
                            snapshot.localBounds.min.y,
                            snapshot.localBounds.min.z);
                        ImGui::Text(
                            "Local Bounds Max: %.2f, %.2f, %.2f",
                            snapshot.localBounds.max.x,
                            snapshot.localBounds.max.y,
                            snapshot.localBounds.max.z);
                        ImGui::Text(
                            "World Bounds Min: %.2f, %.2f, %.2f",
                            snapshot.worldBounds.min.x,
                            snapshot.worldBounds.min.y,
                            snapshot.worldBounds.min.z);
                        ImGui::Text(
                            "World Bounds Max: %.2f, %.2f, %.2f",
                            snapshot.worldBounds.max.x,
                            snapshot.worldBounds.max.y,
                            snapshot.worldBounds.max.z);
                        if (!snapshot.diagnostic.empty())
                        {
                            ImGui::TextWrapped("Diagnostic: %s", snapshot.diagnostic.c_str());
                        }
                    }
                    ImGui::PopID();
                }
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
            ImGui::SliderFloat("Continental Frequency", &_worldGenDraft.shape.continentalFrequency, 0.00005f, 0.0050f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Erosion Frequency", &_worldGenDraft.shape.erosionFrequency, 0.00005f, 0.0050f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Peaks Frequency", &_worldGenDraft.shape.peaksFrequency, 0.00005f, 0.0050f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Detail Frequency", &_worldGenDraft.shape.detailFrequency, 0.00020f, 0.0300f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Sea Level", &_worldGenDraft.shape.seaLevel, 0, static_cast<int>(CHUNK_HEIGHT) - 1);
            ImGui::Checkbox("Enable Rivers", &_worldGenDraft.shape.riversEnabled);
            ImGui::BeginDisabled(!_worldGenDraft.shape.riversEnabled);
            ImGui::SliderFloat("River Frequency", &_worldGenDraft.shape.riverFrequency, 0.00005f, 0.0100f, "%.5f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("River Width Threshold", &_worldGenDraft.shape.riverThreshold, 0.005f, 0.35f, "%.3f");
            ImGui::EndDisabled();
            ImGui::SliderFloat("Continental Strength", &_worldGenDraft.shape.continentalStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Peaks Strength", &_worldGenDraft.shape.peaksStrength, 0.0f, 2.5f, "%.2f");
            ImGui::SliderFloat("Erosion Strength", &_worldGenDraft.shape.erosionStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Valley Strength", &_worldGenDraft.shape.valleyStrength, 0.0f, 64.0f, "%.1f");
            ImGui::SliderFloat("Detail Strength", &_worldGenDraft.shape.detailStrength, 0.0f, 16.0f, "%.1f");
            ImGui::SliderFloat("Erosion Suppression Low", &_worldGenDraft.shape.erosionSuppressionLow, 0.1f, 2.5f, "%.2f");
            ImGui::SliderFloat("Erosion Suppression High", &_worldGenDraft.shape.erosionSuppressionHigh, 0.1f, 2.5f, "%.2f");
            ImGui::TextUnformatted("Biome-driven terrain materials are disabled for now. Terrain rasterizes as stone while decorations and biome enums stay in place for later work.");

            draw_spline_editor("ErosionSplineTable", "Erosion Spline", _worldGenDraft.erosionSplines);
            draw_spline_editor("PeakSplineTable", "Peak Spline", _worldGenDraft.peakSplines);
            draw_spline_editor("ContinentalSplineTable", "Continentalness Spline", _worldGenDraft.continentalSplines);

            ImGui::SeparatorText("Current Area Preview");
            const char* layerLabels[] = {
                "Final Height",
                "Base Height",
                "Continentalness",
                "Erosion",
                "Peaks/Valleys",
                "Detail",
                "River",
                "Biome",
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
	ZoneScopedN("GameScene::UpdateFogUBO");
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
    ZoneScopedN("GameScene::UpdateLightingUBO");
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
    const auto& dynamicLights = _dynamicLightRegistry.snapshot();
    const size_t dynamicLightCount = std::min(dynamicLights.size(), LightingUBO::MaxDynamicLights);
    lighting.params3 = glm::vec4(tuning.shadowStrength, tuning.localLightStrength, static_cast<float>(dynamicLightCount), 0.0f);
    lighting.params4 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    for (size_t index = 0; index < dynamicLightCount; ++index)
    {
        const world_lighting::DynamicPointLight& light = dynamicLights[index];
        lighting.dynamicLightPositionRadius[index] = glm::vec4(light.position, light.radius);
        lighting.dynamicLightColorIntensity[index] = glm::vec4(light.color, light.intensity);
        lighting.dynamicLightMetadata[index] = glm::uvec4(light.affectMask, light.active ? 1u : 0u, 0u, 0u);
    }

    void* data;
    vmaMapMemory(_services.allocator, _lightingResource->value.buffer._allocation, &data);
    memcpy(data, &lighting, sizeof(LightingUBO));
    vmaUnmapMemory(_services.allocator, _lightingResource->value.buffer._allocation);
}

void GameScene::update_uniform_buffer() const
{
	ZoneScopedN("GameScene::UpdateCameraUBO");
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

void GameScene::sync_runtime_lights()
{
    ZoneScopedN("GameScene::SyncRuntimeLights");
    if (!_playerTorchLightEnabled)
    {
        if (_playerTorchLightId.has_value())
        {
            _dynamicLightRegistry.remove(_playerTorchLightId.value());
            _playerTorchLightId.reset();
        }
        return;
    }

    const glm::vec3 lightPosition = _game.snapshot().player.position + glm::vec3(0.0f, 0.3f, 0.0f);
    const world_lighting::DynamicPointLight playerTorch{
        .position = lightPosition,
        .color = _playerTorchColor,
        .intensity = _playerTorchIntensity,
        .radius = _playerTorchRadius,
        .affectMask = world_lighting::AffectAll,
        .active = true
    };

    if (!_playerTorchLightId.has_value())
    {
        _playerTorchLightId = _dynamicLightRegistry.create(playerTorch);
        return;
    }

    _dynamicLightRegistry.update(_playerTorchLightId.value(), playerTorch);
}

void GameScene::sync_camera_to_game(const float deltaTime)
{
    ZoneScopedN("GameScene::SyncCamera");
    const PlayerSnapshot& player = _game.snapshot().player;
    constexpr float ThirdPersonDistance = 5.0f;
    const glm::vec3 target = player.cameraTarget;
    const glm::vec3 cameraOffset = -player.cameraForward * ThirdPersonDistance;
    _camera->_position = target + cameraOffset;
    _camera->_front = glm::normalize(target - _camera->_position);
    _camera->_up = player.up;
    _camera->_yaw = player.cameraYaw;
    _camera->_pitch = player.cameraPitch;
    _camera->update(deltaTime);
}

void GameScene::sync_player_render_instance()
{
    ZoneScopedN("GameScene::SyncPlayerRenderInstance");
    const PlayerEntity* const player = _game.player();
    if (player == nullptr)
    {
        return;
    }

    const VoxelComponentRenderBundle renderBundle =
        build_voxel_component_render_bundle(*player, _game.voxel_assembly_asset_manager(), _game.voxel_asset_manager());
    std::unordered_set<std::string> activePartIds{};
    activePartIds.reserve(renderBundle.entries.size());
    for (const VoxelComponentRenderEntry& entry : renderBundle.entries)
    {
        activePartIds.insert(entry.stableId);

        if (const auto instanceIt = _playerAssemblyInstanceIds.find(entry.stableId);
            instanceIt != _playerAssemblyInstanceIds.end())
        {
            (void)_playerVoxelRenderRegistry.update_instance(instanceIt->second, entry.renderInstance);
        }
        else
        {
            const VoxelRenderRegistry::InstanceId instanceId = _playerVoxelRenderRegistry.add_instance(entry.renderInstance);
            _playerAssemblyInstanceIds.insert_or_assign(entry.stableId, instanceId);
        }
    }

    for (auto it = _playerAssemblyInstanceIds.begin(); it != _playerAssemblyInstanceIds.end();)
    {
        if (!activePartIds.contains(it->first))
        {
            (void)_playerVoxelRenderRegistry.remove_instance(it->second, _renderState);
            it = _playerAssemblyInstanceIds.erase(it);
            continue;
        }

        ++it;
    }

    if (renderBundle.has_error())
    {
        _playerAssemblyStatus = renderBundle.diagnostic;
    }
    else if (player->assembly_render_component().assetId.empty())
    {
        _playerAssemblyStatus = "No player assembly selected.";
    }
    else
    {
        _playerAssemblyStatus = std::format(
            "Rendering player assembly '{}' as {} part(s).",
            player->assembly_render_component().assetId,
            renderBundle.entries.size());
    }
}

void GameScene::sync_spatial_collider_debug()
{
    ZoneScopedN("GameScene::SyncSpatialColliderDebug");
    clear_spatial_collider_debug();
    if (!_showSpatialColliderBounds)
    {
        return;
    }

    const std::vector<DebugSpatialColliderSnapshot> snapshots = _game.debug_spatial_colliders();
    std::unordered_set<std::string> activeSnapshotIds{};
    activeSnapshotIds.reserve(snapshots.size());

    const auto debugMaterial = _services.materialManager->get_material(GameSceneMaterialScope, "chunkboundary");
    _spatialColliderDebugHandles.reserve(snapshots.size());

    for (const DebugSpatialColliderSnapshot& snapshot : snapshots)
    {
        activeSnapshotIds.insert(snapshot.id);
        if (!snapshot.valid)
        {
            continue;
        }

        SpatialColliderDebugMeshCacheEntry& cacheEntry = _spatialColliderDebugMeshCache[snapshot.id];
        if (cacheEntry.mesh == nullptr ||
            !cacheEntry.boundsCached ||
            !equal_aabb(cacheEntry.localBounds, snapshot.localBounds))
        {
            if (cacheEntry.mesh != nullptr)
            {
                render::enqueue_mesh_release(std::move(cacheEntry.mesh));
            }

            cacheEntry.mesh = Mesh::create_box_outline_mesh(
                snapshot.localBounds.min,
                snapshot.localBounds.max,
                glm::vec3(0.28f, 1.0f, 0.45f));
            _services.meshManager->UploadQueue.enqueue(cacheEntry.mesh);
            cacheEntry.localBounds = snapshot.localBounds;
            cacheEntry.boundsCached = true;
        }

        _spatialColliderDebugHandles.push_back(_renderState.transparentObjects.insert(RenderObject{
            .mesh = cacheEntry.mesh,
            .material = debugMaterial,
            .transform = glm::translate(glm::mat4(1.0f), snapshot.origin),
            .layer = RenderLayer::Transparent,
            .lightingMode = LightingMode::Unlit
        }));
    }

    for (auto it = _spatialColliderDebugMeshCache.begin(); it != _spatialColliderDebugMeshCache.end();)
    {
        if (!activeSnapshotIds.contains(it->first))
        {
            if (it->second.mesh != nullptr)
            {
                render::enqueue_mesh_release(std::move(it->second.mesh));
            }
            it = _spatialColliderDebugMeshCache.erase(it);
            continue;
        }

        ++it;
    }
}

void GameScene::refresh_player_assembly_assets()
{
    _savedPlayerAssemblyIds = _game.voxel_assembly_repository().list_asset_ids();
    _selectedPlayerAssemblyIndex = -1;

    if (_playerAssemblyAssetId.empty())
    {
        return;
    }

    for (int index = 0; index < static_cast<int>(_savedPlayerAssemblyIds.size()); ++index)
    {
        if (_savedPlayerAssemblyIds[static_cast<size_t>(index)] == _playerAssemblyAssetId)
        {
            _selectedPlayerAssemblyIndex = index;
            break;
        }
    }
}

void GameScene::sync_target_block()
{
    ZoneScopedN("GameScene::SyncTargetBlock");
    _targetBlock = _game.raycast_target_block(_camera->_position, _camera->_front, GameConfig::BLOCK_INTERACTION_DISTANCE);
}

void GameScene::sync_target_block_outline()
{
    ZoneScopedN("GameScene::SyncTargetBlockOutline");
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

    _targetBlockOutlineHandle = _renderState.transparentObjects.insert(RenderObject{
        .mesh = _targetBlockOutlineMesh,
        .material = _services.materialManager->get_material(GameSceneMaterialScope, "chunkboundary"),
        .transform = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(chunkOrigin.x),
            0.0f,
            static_cast<float>(chunkOrigin.y))),
        .layer = RenderLayer::Transparent,
        .lightingMode = LightingMode::Unlit
    });
}

void GameScene::sync_chunk_boundary_debug()
{
    ZoneScopedN("GameScene::SyncChunkBoundaryDebug");
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

    const auto debugMaterial = _services.materialManager->get_material(GameSceneMaterialScope, "chunkboundary");
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

            _chunkBoundaryHandles.push_back(_renderState.transparentObjects.insert(RenderObject{
            .mesh = _chunkBoundaryMesh,
            .material = debugMaterial,
            .transform = glm::translate(glm::mat4(1.0f), glm::vec3(
                static_cast<float>(chunk->_data->position.x),
                0.0f,
                static_cast<float>(chunk->_data->position.y))),
            .layer = RenderLayer::Transparent,
            .lightingMode = LightingMode::Unlit
        }));
        }
    }
}

void GameScene::rebuild_runtime_voxel_demo()
{
    ZoneScopedN("GameScene::RebuildRuntimeVoxelDemo");
    _runtimeVoxelDemoInitialized = true;
    _runtimeVoxelDemoDirty = false;
    _voxelRenderRegistry.clear(_renderState);

    if (_game.voxel_asset_manager().find_loaded(_runtimeVoxelAssetId) == nullptr &&
        _game.voxel_asset_manager().load_or_get(_runtimeVoxelAssetId) == nullptr)
    {
        _runtimeVoxelStatus = std::format(
            "Could not load voxel asset '{}'. Save a model in the voxel editor first.",
            _runtimeVoxelAssetId);
        return;
    }

    static constexpr std::array<glm::ivec2, 5> offsets{
        glm::ivec2{-3, 2},
        glm::ivec2{-1, 3},
        glm::ivec2{1, 2},
        glm::ivec2{3, 3},
        glm::ivec2{0, 5}
    };

    for (size_t index = 0; index < offsets.size(); ++index)
    {
        const float yawDegrees = 22.5f + (static_cast<float>(index) * 37.0f);
        VoxelModelComponent component{};
        component.assetId = _runtimeVoxelAssetId;
        component.position = runtime_voxel_demo_position(offsets[index]);
        component.rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        component.scale = 1.0f;
        component.placementPolicy = VoxelPlacementPolicy::BottomCenter;
        component.visible = true;

        const std::optional<VoxelRenderInstance> renderInstance =
            build_voxel_render_instance(component, _game.voxel_asset_manager());
        if (!renderInstance.has_value())
        {
            continue;
        }

        (void)_voxelRenderRegistry.add_instance(renderInstance.value());
    }

    _runtimeVoxelStatus = std::format(
        "Loaded '{}' as {} shared-mesh runtime prop instance(s).",
        _runtimeVoxelAssetId,
        offsets.size());
}

glm::vec3 GameScene::runtime_voxel_demo_position(const glm::ivec2& offset) const
{
    const PlayerSnapshot& player = _game.snapshot().player;
    const int worldX = static_cast<int>(std::floor(player.position.x)) + offset.x;
    const int worldZ = static_cast<int>(std::floor(player.position.z)) + offset.y;
    const TerrainColumnSample column = TerrainGenerator::instance().SampleColumn(worldX, worldZ);

    return glm::vec3(
        static_cast<float>(worldX) + 0.5f,
        static_cast<float>(column.surfaceHeight + 1),
        static_cast<float>(worldZ) + 0.5f);
}

void GameScene::clear_chunk_boundary_debug()
{
    for (const auto handle : _chunkBoundaryHandles)
    {
        _renderState.transparentObjects.remove(handle);
    }

    _chunkBoundaryHandles.clear();
}

void GameScene::clear_target_block_outline()
{
    if (_targetBlockOutlineHandle.has_value())
    {
        _renderState.transparentObjects.remove(_targetBlockOutlineHandle.value());
        _targetBlockOutlineHandle.reset();
    }
}

void GameScene::clear_spatial_collider_debug()
{
    for (const auto handle : _spatialColliderDebugHandles)
    {
        _renderState.transparentObjects.remove(handle);
    }

    _spatialColliderDebugHandles.clear();
}

void GameScene::release_spatial_collider_debug_meshes()
{
    for (auto& [id, cacheEntry] : _spatialColliderDebugMeshCache)
    {
        if (cacheEntry.mesh != nullptr)
        {
            render::enqueue_mesh_release(std::move(cacheEntry.mesh));
        }
    }

    _spatialColliderDebugMeshCache.clear();
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
    _settings.bind_player_settings_handler([this](const settings::PlayerRuntimeSettings& settings)
    {
        apply_player_settings(settings);
    });
}

void GameScene::apply_view_distance_settings(const settings::ViewDistanceRuntimeSettings& settings)
{
    _viewDistanceDraft = settings.viewDistance;
    _chunkRenderRegistry.clear(_renderState);
    _chunkDecorationRenderRegistry.clear(_renderState);
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

void GameScene::apply_player_settings(const settings::PlayerRuntimeSettings& settings)
{
    CharacterBodyComponent body{};
    body.cameraTargetOffset = settings.cameraTargetOffset;

    _game.configure_player(
        PlayerPhysicsTuning{
            .moveSpeed = settings.moveSpeed,
            .airControl = settings.airControl,
            .gravity = settings.gravity,
            .jumpVelocity = settings.jumpVelocity,
            .maxFallSpeed = settings.maxFallSpeed
        },
        body,
        settings.flyModeEnabled);
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
