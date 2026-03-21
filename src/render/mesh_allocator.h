#ifndef MESH_ALLOCATOR_H
#define MESH_ALLOCATOR_H

#include <memory>
#include <vector>

#include "constants.h"
#include "vk_types.h"

enum class MeshAllocationStrategy : uint8_t
{
    Arena = 0,
    VariableSuballocation = 1
};

class IMeshAllocator;

struct MeshAllocatorConfig
{
    MeshAllocationStrategy strategy{MeshAllocationStrategy::VariableSuballocation};
    size_t slotCapacity{static_cast<size_t>((maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE) * 2) + 128)};
    VkDeviceSize vertexSlabSize{1048576};
    VkDeviceSize indexSlabSize{262144};
    VkDeviceSize vertexBufferSize{vertexSlabSize * slotCapacity};
    VkDeviceSize indexBufferSize{indexSlabSize * slotCapacity};
};

struct MeshAllocation
{
    VkDeviceSize vertexOffset{0};
    VkDeviceSize indexOffset{0};
    VkDeviceSize vertexSize{0};
    VkDeviceSize indexSize{0};
    int32_t handle{-1};
    uint32_t indicesSize{0};
    IMeshAllocator* allocator{nullptr};
};

class IMeshAllocator
{
public:
    virtual ~IMeshAllocator() = default;

    [[nodiscard]] virtual MeshAllocation acquire(VkDeviceSize vertexSize, VkDeviceSize indexSize) = 0;
    virtual void free(MeshAllocation allocation) = 0;
    virtual void reconfigure(MeshAllocatorConfig config) = 0;
    [[nodiscard]] virtual bool can_reconfigure() const noexcept = 0;
    [[nodiscard]] virtual const MeshAllocatorConfig& config() const noexcept = 0;
    [[nodiscard]] virtual VkBuffer vertex_buffer_handle() const noexcept = 0;
    [[nodiscard]] virtual VkBuffer index_buffer_handle() const noexcept = 0;
};

class ArenaMeshAllocator final : public IMeshAllocator
{
public:
    explicit ArenaMeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config = {});
    ~ArenaMeshAllocator() override;

    [[nodiscard]] MeshAllocation acquire(VkDeviceSize vertexSize, VkDeviceSize indexSize) override;
    void free(MeshAllocation allocation) override;
    void reconfigure(MeshAllocatorConfig config) override;
    [[nodiscard]] bool can_reconfigure() const noexcept override;
    [[nodiscard]] const MeshAllocatorConfig& config() const noexcept override;
    [[nodiscard]] VkBuffer vertex_buffer_handle() const noexcept override;
    [[nodiscard]] VkBuffer index_buffer_handle() const noexcept override;

private:
    struct MeshSlot
    {
        VkDeviceSize vertexOffset{0};
        VkDeviceSize indexOffset{0};
    };

    std::vector<int32_t> m_free_list{};
    VmaAllocator m_allocator{};
    MeshAllocatorConfig m_config{};
    AllocatedBuffer m_indexBuffer{};
    AllocatedBuffer m_vertexBuffer{};

    [[nodiscard]] MeshSlot get_slot(size_t index) const;
    void destroy_buffers();
    void create_buffers();
};

class VariableMeshAllocator final : public IMeshAllocator
{
public:
    explicit VariableMeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config = {});
    ~VariableMeshAllocator() override;

    [[nodiscard]] MeshAllocation acquire(VkDeviceSize vertexSize, VkDeviceSize indexSize) override;
    void free(MeshAllocation allocation) override;
    void reconfigure(MeshAllocatorConfig config) override;
    [[nodiscard]] bool can_reconfigure() const noexcept override;
    [[nodiscard]] const MeshAllocatorConfig& config() const noexcept override;
    [[nodiscard]] VkBuffer vertex_buffer_handle() const noexcept override;
    [[nodiscard]] VkBuffer index_buffer_handle() const noexcept override;

private:
    struct FreeRange
    {
        VkDeviceSize offset{0};
        VkDeviceSize size{0};
    };

    struct AllocationRecord
    {
        VkDeviceSize vertexOffset{0};
        VkDeviceSize vertexSize{0};
        VkDeviceSize indexOffset{0};
        VkDeviceSize indexSize{0};
        bool live{false};
    };

    VmaAllocator m_allocator{};
    MeshAllocatorConfig m_config{};
    AllocatedBuffer m_indexBuffer{};
    AllocatedBuffer m_vertexBuffer{};
    std::vector<FreeRange> m_freeVertexRanges{};
    std::vector<FreeRange> m_freeIndexRanges{};
    std::vector<AllocationRecord> m_records{};
    std::vector<int32_t> m_freeHandles{};
    size_t m_liveAllocations{0};

    [[nodiscard]] static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) noexcept;
    [[nodiscard]] VkDeviceSize allocate_range(std::vector<FreeRange>& ranges, VkDeviceSize size, VkDeviceSize alignment);
    static void free_range(std::vector<FreeRange>& ranges, VkDeviceSize offset, VkDeviceSize size);
    void destroy_buffers();
    void create_buffers();
};

[[nodiscard]] std::unique_ptr<IMeshAllocator> create_mesh_allocator(VmaAllocator allocator, const MeshAllocatorConfig& config);

#endif // MESH_ALLOCATOR_H
