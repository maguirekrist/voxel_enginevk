#include "chunk_scheduler.h"

bool ChunkScheduler::should_generate(const ChunkRecord& record) const noexcept
{
    return record.residency == ChunkResidencyState::Resident &&
        record.dataState == DataState::Empty &&
        !record.generationJobInFlight;
}

bool ChunkScheduler::should_light(const ChunkRecord& record, const bool requiredNeighborsReady, const uint64_t desiredSignature) const noexcept
{
    if (record.residency != ChunkResidencyState::Resident || !requiredNeighborsReady || record.lightJobInFlight)
    {
        return false;
    }

    if (record.dataState != DataState::Ready && record.dataState != DataState::Dirty)
    {
        return false;
    }

    if (record.lightState == LightState::Missing || record.lightState == LightState::Stale)
    {
        return true;
    }

    if (record.lightState == LightState::Ready)
    {
        return record.litAgainstSignature != desiredSignature;
    }

    return false;
}

bool ChunkScheduler::should_mesh(const ChunkRecord& record, const bool requiredNeighborsReady, const uint64_t desiredSignature) const noexcept
{
    if (record.residency != ChunkResidencyState::Resident || !requiredNeighborsReady || record.meshJobInFlight)
    {
        return false;
    }

    if (record.dataState != DataState::Ready && record.dataState != DataState::Dirty)
    {
        return false;
    }

    if (record.lightState != LightState::Ready)
    {
        return false;
    }

    if (record.meshState == MeshState::Missing || record.meshState == MeshState::Stale)
    {
        return true;
    }

    if (record.meshState == MeshState::Uploaded || record.meshState == MeshState::MeshReady)
    {
        return record.meshedAgainstSignature != desiredSignature;
    }

    return false;
}

bool ChunkScheduler::should_upload(const ChunkRecord& record) const noexcept
{
    return record.residency == ChunkResidencyState::Resident &&
        record.meshState == MeshState::MeshReady &&
        !record.uploadPending &&
        record.uploadedSignature != record.meshedAgainstSignature;
}
