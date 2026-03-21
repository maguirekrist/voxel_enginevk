#pragma once

#include <unordered_map>

#include "chunk_cache.h"
#include "chunk_dirty_tracker.h"
#include "chunk_lighting.h"
#include "chunk_neighborhood.h"
#include "chunk_record.h"
#include "chunk_scheduler.h"
#include "thread_pool.h"
#include "world_edit_queue.h"


struct MapRange
{
    int low_x{0};
    int high_x{0};
    int low_z{0};
    int high_z{0};

    MapRange() = default;

    MapRange(const ChunkCoord center, const int viewDistance) :
    low_x(center.x - viewDistance), high_x(center.x + viewDistance), low_z(center.z - viewDistance), high_z(center.z + viewDistance)
    {
    }

    [[nodiscard]] constexpr bool contains(const ChunkCoord& coord) const
    {
        return (coord.x >= low_x &&
            coord.x <= high_x &&
            coord.z >= low_z &&
            coord.z <= high_z);
    }

    [[nodiscard]] constexpr bool is_border(const ChunkCoord& coord) const
    {
        return (coord.x == low_x ||
            coord.x == high_x ||
            coord.z == low_z ||
            coord.z == high_z);
    }
};

struct ChunkStreamingSettings
{
    int viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
};

struct ChunkMeshSettings
{
    bool ambientOcclusionEnabled{false};
};

class ChunkManager {
public:
    struct ChunkRenderResetEvent
    {
        Chunk* chunk{};
        uint32_t generation{};
    };

    struct ChunkRenderReadyEvent
    {
        Chunk* chunk{};
        uint32_t generationId{};
        uint64_t neighborhoodSignature{};
        std::shared_ptr<ChunkData> data;
        std::shared_ptr<ChunkMeshData> meshData;
    };

    struct ChunkDebugState
    {
        bool resident{false};
        uint32_t generationId{0};
        uint32_t dataVersion{0};
        uint32_t lightVersion{0};
        uint64_t litAgainstSignature{0};
        uint64_t meshedAgainstSignature{0};
        uint64_t uploadedSignature{0};
        DataState dataState{DataState::Empty};
        LightState lightState{LightState::Missing};
        MeshState meshState{MeshState::Missing};
        bool generationJobInFlight{false};
        bool lightJobInFlight{false};
        bool meshJobInFlight{false};
        bool uploadPending{false};
    };

    std::unique_ptr<ChunkCache> m_chunkCache;

    ChunkManager();
    ~ChunkManager();

    void update_player_position(const glm::vec3& position);
    void enqueue_block_edit(const BlockEdit& edit);
    void notify_chunk_uploaded(Chunk* chunk, uint32_t generationId, uint64_t neighborhoodSignature);
    Chunk* get_chunk(ChunkCoord coord) const;
    std::optional<ChunkDebugState> debug_state(ChunkCoord coord) const;
    std::optional<ChunkNeighborhood> build_neighborhood(ChunkCoord coord) const;
    void apply_mesh_settings(const ChunkMeshSettings& settings);
    void regenerate_world();
    [[nodiscard]] bool ambient_occlusion_enabled() const noexcept;
    void apply_streaming_settings(const ChunkStreamingSettings& settings);
    [[nodiscard]] int view_distance() const noexcept;
    bool try_dequeue_render_reset(ChunkRenderResetEvent& event);
    bool try_dequeue_render_ready(ChunkRenderReadyEvent& event);

private:
    struct ChunkGenerateResult
    {
        Chunk* chunk{};
        uint32_t generationId{};
        std::shared_ptr<ChunkData> data{};
    };

    struct ChunkMeshBuildResult
    {
        Chunk* chunk{};
        ChunkCoord coord{};
        uint32_t generationId{};
        uint32_t dataVersion{};
        uint64_t neighborhoodSignature{};
        std::shared_ptr<ChunkMeshData> meshData{};
    };

    struct ChunkLightBuildResult
    {
        Chunk* chunk{};
        ChunkCoord coord{};
        uint32_t generationId{};
        uint32_t dataVersion{};
        uint64_t neighborhoodSignature{};
        std::shared_ptr<ChunkData> litData{};
    };

    struct ChunkRuntime
    {
        ChunkRecord record{};
    };

    void initialize_map(MapRange mapRange);
    void drain_generate_results();
    void drain_light_results();
    void drain_mesh_results();
    void apply_pending_world_edits();
    void run_scheduler();
    void reset_chunk_runtime(Chunk* chunk);
    void mark_chunk_dirty(Chunk* chunk, bool dataChanged, bool lightingInvalidated);
    void queue_generate(Chunk* chunk);
    void queue_light(Chunk* chunk, uint64_t neighborhoodSignature, const ChunkNeighborhood& neighborhood);
    void queue_mesh(Chunk* chunk, uint64_t neighborhoodSignature, const ChunkNeighborhood& neighborhood);
    [[nodiscard]] ChunkRuntime* runtime_for(const Chunk* chunk);
    [[nodiscard]] const ChunkRuntime* runtime_for(const Chunk* chunk) const;
    [[nodiscard]] std::optional<ChunkNeighborhood> build_light_neighborhood(ChunkCoord coord) const;
    [[nodiscard]] uint64_t compute_light_signature(const ChunkNeighborhood& neighborhood) const;
    [[nodiscard]] uint64_t compute_mesh_signature(const ChunkNeighborhood& neighborhood) const;
    [[nodiscard]] bool required_neighbors_have_data(ChunkCoord coord, uint64_t& signature, ChunkNeighborhood& neighborhood) const;
    [[nodiscard]] bool required_neighbors_have_lighting(ChunkCoord coord, uint64_t& signature, ChunkNeighborhood& neighborhood) const;

    bool _initialLoad{true};
    bool _ambientOcclusionEnabled{false};
    int _viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
    ChunkCoord _lastPlayerChunk = {0, 0};

    std::unordered_map<Chunk*, ChunkRuntime> _runtimeByChunk;
    moodycamel::BlockingConcurrentQueue<ChunkRenderResetEvent> _renderResetEvents;
    moodycamel::BlockingConcurrentQueue<ChunkRenderReadyEvent> _renderReadyEvents;
    moodycamel::BlockingConcurrentQueue<ChunkGenerateResult> _generateResults;
    moodycamel::BlockingConcurrentQueue<ChunkLightBuildResult> _lightResults;
    moodycamel::BlockingConcurrentQueue<ChunkMeshBuildResult> _meshResults;

    ThreadPool _threadPool{4};
    ChunkScheduler _scheduler{};
    ChunkDirtyTracker _dirtyTracker{};
    WorldEditQueue _worldEditQueue{};
};
