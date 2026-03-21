#include "mesh_allocator.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

#include "mesh.h"
#include "vk_util.h"

namespace
{
    constexpr VkDeviceSize VertexAlignment = alignof(Vertex);
    constexpr VkDeviceSize IndexAlignment = alignof(uint32_t);

    MeshAllocatorConfig normalized_config(MeshAllocatorConfig config)
    {
        if (config.slotCapacity == 0)
        {
            config.slotCapacity = 1;
        }

        config.vertexSlabSize = std::max<VkDeviceSize>(config.vertexSlabSize, 256 * 1024);
        config.indexSlabSize = std::max<VkDeviceSize>(config.indexSlabSize, 64 * 1024);
        config.vertexBufferSize = std::max<VkDeviceSize>(config.vertexBufferSize, config.vertexSlabSize * config.slotCapacity);
        config.indexBufferSize = std::max<VkDeviceSize>(config.indexBufferSize, config.indexSlabSize * config.slotCapacity);
        return config;
    }
}

ArenaMeshAllocator::ArenaMeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config) :
    m_allocator(allocator)
{
    reconfigure(std::move(config));
}

ArenaMeshAllocator::~ArenaMeshAllocator()
{
    destroy_buffers();
}

MeshAllocation ArenaMeshAllocator::acquire(const VkDeviceSize vertexSize, const VkDeviceSize indexSize)
{
    if (m_free_list.empty())
    {
        throw std::runtime_error("ArenaMeshAllocator::acquire: no free slots");
    }

    const int32_t freeIndex = m_free_list.back();
    m_free_list.pop_back();
    const MeshSlot slot = get_slot(static_cast<size_t>(freeIndex));

    return MeshAllocation{
        .vertexOffset = slot.vertexOffset,
        .indexOffset = slot.indexOffset,
        .vertexSize = vertexSize,
        .indexSize = indexSize,
        .handle = freeIndex,
        .allocator = this
    };
}

void ArenaMeshAllocator::free(const MeshAllocation allocation)
{
    if (allocation.handle < 0)
    {
        return;
    }

    m_free_list.push_back(allocation.handle);
}

void ArenaMeshAllocator::reconfigure(MeshAllocatorConfig config)
{
    if (!can_reconfigure() && (m_vertexBuffer._buffer != VK_NULL_HANDLE || m_indexBuffer._buffer != VK_NULL_HANDLE))
    {
        throw std::runtime_error("ArenaMeshAllocator::reconfigure: allocator still has live mesh allocations");
    }

    m_config = normalized_config(std::move(config));
    destroy_buffers();
    create_buffers();

    m_free_list.resize(m_config.slotCapacity);
    for (size_t i = 0; i < m_config.slotCapacity; ++i)
    {
        m_free_list[i] = static_cast<int32_t>(i);
    }
}

bool ArenaMeshAllocator::can_reconfigure() const noexcept
{
    return m_free_list.size() == m_config.slotCapacity;
}

const MeshAllocatorConfig& ArenaMeshAllocator::config() const noexcept
{
    return m_config;
}

VkBuffer ArenaMeshAllocator::vertex_buffer_handle() const noexcept
{
    return m_vertexBuffer._buffer;
}

VkBuffer ArenaMeshAllocator::index_buffer_handle() const noexcept
{
    return m_indexBuffer._buffer;
}

ArenaMeshAllocator::MeshSlot ArenaMeshAllocator::get_slot(const size_t index) const
{
    if (index >= m_config.slotCapacity)
    {
        throw std::runtime_error("ArenaMeshAllocator::get_slot: index out of range");
    }

    return MeshSlot{
        .vertexOffset = index * m_config.vertexSlabSize,
        .indexOffset = index * m_config.indexSlabSize
    };
}

void ArenaMeshAllocator::destroy_buffers()
{
    if (m_vertexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_vertexBuffer._buffer, m_vertexBuffer._allocation);
        m_vertexBuffer = {};
    }

    if (m_indexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_indexBuffer._buffer, m_indexBuffer._allocation);
        m_indexBuffer = {};
    }
}

void ArenaMeshAllocator::create_buffers()
{
    const VkDeviceSize vertexBufferSize = m_config.vertexSlabSize * m_config.slotCapacity;
    const VkDeviceSize indexBufferSize = m_config.indexSlabSize * m_config.slotCapacity;

    m_vertexBuffer = vkutil::create_buffer(
        m_allocator,
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_indexBuffer = vkutil::create_buffer(
        m_allocator,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

VariableMeshAllocator::VariableMeshAllocator(VmaAllocator allocator, MeshAllocatorConfig config) :
    m_allocator(allocator)
{
    reconfigure(std::move(config));
}

VariableMeshAllocator::~VariableMeshAllocator()
{
    destroy_buffers();
}

MeshAllocation VariableMeshAllocator::acquire(const VkDeviceSize vertexSize, const VkDeviceSize indexSize)
{
    const VkDeviceSize vertexOffset = allocate_range(m_freeVertexRanges, vertexSize, VertexAlignment);
    try
    {
        const VkDeviceSize indexOffset = allocate_range(m_freeIndexRanges, indexSize, IndexAlignment);

        int32_t handle = -1;
        if (!m_freeHandles.empty())
        {
            handle = m_freeHandles.back();
            m_freeHandles.pop_back();
            m_records[handle] = AllocationRecord{
                .vertexOffset = vertexOffset,
                .vertexSize = vertexSize,
                .indexOffset = indexOffset,
                .indexSize = indexSize,
                .live = true
            };
        }
        else
        {
            handle = static_cast<int32_t>(m_records.size());
            m_records.push_back(AllocationRecord{
                .vertexOffset = vertexOffset,
                .vertexSize = vertexSize,
                .indexOffset = indexOffset,
                .indexSize = indexSize,
                .live = true
            });
        }

        ++m_liveAllocations;
        return MeshAllocation{
            .vertexOffset = vertexOffset,
            .indexOffset = indexOffset,
            .vertexSize = vertexSize,
            .indexSize = indexSize,
            .handle = handle,
            .allocator = this
        };
    }
    catch (...)
    {
        free_range(m_freeVertexRanges, vertexOffset, vertexSize);
        throw;
    }
}

void VariableMeshAllocator::free(const MeshAllocation allocation)
{
    if (allocation.handle < 0 || allocation.handle >= static_cast<int32_t>(m_records.size()))
    {
        return;
    }

    AllocationRecord& record = m_records[allocation.handle];
    if (!record.live)
    {
        return;
    }

    free_range(m_freeVertexRanges, record.vertexOffset, record.vertexSize);
    free_range(m_freeIndexRanges, record.indexOffset, record.indexSize);
    record.live = false;
    m_freeHandles.push_back(allocation.handle);
    if (m_liveAllocations > 0)
    {
        --m_liveAllocations;
    }
}

void VariableMeshAllocator::reconfigure(MeshAllocatorConfig config)
{
    if (!can_reconfigure() && (m_vertexBuffer._buffer != VK_NULL_HANDLE || m_indexBuffer._buffer != VK_NULL_HANDLE))
    {
        throw std::runtime_error("VariableMeshAllocator::reconfigure: allocator still has live mesh allocations");
    }

    m_config = normalized_config(std::move(config));
    destroy_buffers();
    create_buffers();

    m_freeVertexRanges = { FreeRange{0, m_config.vertexBufferSize} };
    m_freeIndexRanges = { FreeRange{0, m_config.indexBufferSize} };
    m_records.clear();
    m_freeHandles.clear();
    m_liveAllocations = 0;
}

bool VariableMeshAllocator::can_reconfigure() const noexcept
{
    return m_liveAllocations == 0;
}

const MeshAllocatorConfig& VariableMeshAllocator::config() const noexcept
{
    return m_config;
}

VkBuffer VariableMeshAllocator::vertex_buffer_handle() const noexcept
{
    return m_vertexBuffer._buffer;
}

VkBuffer VariableMeshAllocator::index_buffer_handle() const noexcept
{
    return m_indexBuffer._buffer;
}

VkDeviceSize VariableMeshAllocator::align_up(const VkDeviceSize value, const VkDeviceSize alignment) noexcept
{
    if (alignment <= 1)
    {
        return value;
    }

    return (value + alignment - 1) & ~(alignment - 1);
}

VkDeviceSize VariableMeshAllocator::allocate_range(std::vector<FreeRange>& ranges, const VkDeviceSize size, const VkDeviceSize alignment)
{
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        const VkDeviceSize alignedOffset = align_up(ranges[i].offset, alignment);
        const VkDeviceSize padding = alignedOffset - ranges[i].offset;
        if (ranges[i].size < padding + size)
        {
            continue;
        }

        const VkDeviceSize rangeEnd = ranges[i].offset + ranges[i].size;
        const VkDeviceSize allocEnd = alignedOffset + size;
        if (padding == 0 && allocEnd == rangeEnd)
        {
            ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else if (padding == 0)
        {
            ranges[i].offset = allocEnd;
            ranges[i].size = rangeEnd - allocEnd;
        }
        else if (allocEnd == rangeEnd)
        {
            ranges[i].size = padding;
        }
        else
        {
            const FreeRange tail{allocEnd, rangeEnd - allocEnd};
            ranges[i].size = padding;
            ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(i + 1), tail);
        }

        return alignedOffset;
    }

    throw std::runtime_error(std::format("VariableMeshAllocator::allocate_range: no contiguous range for {} bytes", size));
}

void VariableMeshAllocator::free_range(std::vector<FreeRange>& ranges, const VkDeviceSize offset, const VkDeviceSize size)
{
    if (size == 0)
    {
        return;
    }

    ranges.push_back(FreeRange{offset, size});
    std::ranges::sort(ranges, [](const FreeRange& lhs, const FreeRange& rhs)
    {
        return lhs.offset < rhs.offset;
    });

    std::vector<FreeRange> merged{};
    merged.reserve(ranges.size());
    for (const FreeRange& range : ranges)
    {
        if (merged.empty())
        {
            merged.push_back(range);
            continue;
        }

        FreeRange& last = merged.back();
        const VkDeviceSize lastEnd = last.offset + last.size;
        if (range.offset <= lastEnd)
        {
            last.size = std::max(lastEnd, range.offset + range.size) - last.offset;
            continue;
        }

        merged.push_back(range);
    }

    ranges = std::move(merged);
}

void VariableMeshAllocator::destroy_buffers()
{
    if (m_vertexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_vertexBuffer._buffer, m_vertexBuffer._allocation);
        m_vertexBuffer = {};
    }

    if (m_indexBuffer._buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_indexBuffer._buffer, m_indexBuffer._allocation);
        m_indexBuffer = {};
    }
}

void VariableMeshAllocator::create_buffers()
{
    m_vertexBuffer = vkutil::create_buffer(
        m_allocator,
        m_config.vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_indexBuffer = vkutil::create_buffer(
        m_allocator,
        m_config.indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

std::unique_ptr<IMeshAllocator> create_mesh_allocator(VmaAllocator allocator, const MeshAllocatorConfig& config)
{
    switch (config.strategy)
    {
    case MeshAllocationStrategy::Arena:
        return std::make_unique<ArenaMeshAllocator>(allocator, config);
    case MeshAllocationStrategy::VariableSuballocation:
        return std::make_unique<VariableMeshAllocator>(allocator, config);
    default:
        return std::make_unique<ArenaMeshAllocator>(allocator, config);
    }
}
