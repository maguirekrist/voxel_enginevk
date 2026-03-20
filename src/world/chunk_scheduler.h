#pragma once

#include "chunk_record.h"

class ChunkScheduler
{
public:
    [[nodiscard]] bool should_generate(const ChunkRecord& record) const noexcept;
    [[nodiscard]] bool should_mesh(const ChunkRecord& record, bool requiredNeighborsReady, uint64_t desiredSignature) const noexcept;
    [[nodiscard]] bool should_upload(const ChunkRecord& record) const noexcept;
};
