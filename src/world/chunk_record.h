#pragma once

#include <cstdint>
#include <memory>

#include "game/chunk.h"

enum class ChunkResidencyState : uint8_t
{
    Absent = 0,
    Resident = 1
};

enum class DataState : uint8_t
{
    Empty = 0,
    GenerateQueued = 1,
    Generating = 2,
    Ready = 3,
    Dirty = 4
};

enum class MeshState : uint8_t
{
    Missing = 0,
    MeshQueued = 1,
    Meshing = 2,
    MeshReady = 3,
    Uploaded = 4,
    Stale = 5
};

enum class LightState : uint8_t
{
    Missing = 0,
    LightQueued = 1,
    Lighting = 2,
    Ready = 3,
    Stale = 4
};

struct ChunkRecord
{
    ChunkCoord coord{0, 0};
    std::shared_ptr<ChunkData> data{};
    std::shared_ptr<ChunkMeshData> mesh{};

    uint32_t chunkGenerationId{1};
    uint32_t dataVersion{0};
    uint32_t lightVersion{0};
    uint64_t litAgainstSignature{0};
    uint64_t meshedAgainstSignature{0};
    uint64_t uploadedSignature{0};

    ChunkResidencyState residency{ChunkResidencyState::Resident};
    DataState dataState{DataState::Empty};
    LightState lightState{LightState::Missing};
    MeshState meshState{MeshState::Missing};

    bool generationJobInFlight{false};
    bool lightJobInFlight{false};
    bool meshJobInFlight{false};
    bool uploadPending{false};
};
