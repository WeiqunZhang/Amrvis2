#pragma once

#include <amrvis/cache/BlockKey.hpp>
#include <amrvis/cache/ByteLruCache.hpp>
#include <amrvis/core/StopToken.hpp>
#include <amrvis/io/PlotfileBlockReader.hpp>
#include <amrvis/io/PlotfileMetadataReader.hpp>
#include <amrvis/io/ParticleReader.hpp>

#include <cstdint>
#include <filesystem>

namespace amrvis {

class PlotfileDataset {
public:
    using BlockCache = ByteLruCache<BlockKey, FabBlock, BlockKeyHash>;

    struct BlockAccess {
        BlockCache::Handle handle;
        bool cacheHit = false;
        BlockReadMetrics io;
    };

    PlotfileDataset(
        std::filesystem::path plotfile, DatasetId id, std::uint64_t cacheBudgetBytes);

    [[nodiscard]] const DatasetMetadata& metadata() const noexcept;
    [[nodiscard]] const MetadataReadMetrics& metadataReadMetrics() const noexcept;
    [[nodiscard]] DatasetId id() const noexcept;
    [[nodiscard]] const std::vector<ParticleSpeciesMetadata>& particleSpecies()
        const noexcept;
    [[nodiscard]] ParticleSample requestParticleSample(
        const std::string& species, double fraction, std::uint64_t seed = 0,
        StopToken cancellation = {}) const;

    [[nodiscard]] BlockAccess requestBlock(
        const BlockRequest& request, StopToken cancellation = {});

    [[nodiscard]] CacheMetrics cacheMetrics() const;
    [[nodiscard]] bool setCacheBudget(std::uint64_t bytes);
    void clearUnpinnedCache();

private:
    std::filesystem::path m_plotfile;
    DatasetId m_id;
    PlotfileMetadataResult m_metadataResult;
    std::vector<ParticleSpeciesMetadata> m_particleSpecies;
    PlotfileBlockReader m_blockReader;
    BlockCache m_cache;
};

} // namespace amrvis
