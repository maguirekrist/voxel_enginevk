#pragma once

#include <optional>

#include <utils/blockingconcurrentqueue.h>

#include "mesh.h"
#include "settings/game_settings.h"
#include "staging_buffer.h"

class MeshManager {
public:
    void init(VkDevice device, VmaAllocator allocator, const QueueFamily& queue);
    void apply_view_distance_settings(const settings::ViewDistanceRuntimeSettings& settings);
    [[nodiscard]] bool accepts_uploads() const noexcept;

    void cleanup();
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<Mesh>> UploadQueue;

    void unload_garbage();
    void handle_transfers();

private:
    struct MeshBudget
    {
        int viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
        size_t maximumResidentChunks{static_cast<size_t>(maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE))};
        size_t slotCapacity{static_cast<size_t>((maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE) * 2) + 128)};
    };

    VkDevice m_device{};
    VmaAllocator m_allocator{};
    QueueFamily m_transferQueue{};
    UploadContext m_uploadContext{};

    std::unique_ptr<StagingBuffer> m_stagingBuffer = nullptr;
    MeshBudget m_activeBudget{};
    std::optional<MeshBudget> m_pendingBudget{};

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const;
    void try_apply_pending_budget();
    [[nodiscard]] static MeshBudget make_mesh_budget(const settings::ViewDistanceRuntimeSettings& settings);
    [[nodiscard]] static StagingBufferConfig make_staging_buffer_config(const MeshBudget& budget);

    // void upload_mesh(std::shared_ptr<Mesh>&& mesh) const;
    void unload_mesh(std::shared_ptr<Mesh>&& mesh) const;
};
