#pragma once

#include <amrvis/cache/BlockKey.hpp>
#include <amrvis/cache/ByteLruCache.hpp>
#include <amrvis/core/StopToken.hpp>
#include <amrvis/io/PlotfileBlockReader.hpp>
#include <amrvis/io/PlotfileMetadataReader.hpp>

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
    PlotfileDataset(std::filesystem::path dataRoot, DatasetId id,
        std::uint64_t cacheBudgetBytes, PlotfileMetadataResult metadata);

    [[nodiscard]] const DatasetMetadata& metadata() const noexcept;
    [[nodiscard]] const MetadataReadMetrics& metadataReadMetrics() const noexcept;
    [[nodiscard]] DatasetId id() const noexcept;
    [[nodiscard]] const std::filesystem::path& dataRoot() const noexcept;

    [[nodiscard]] BlockAccess requestBlock(
        const BlockRequest& request, StopToken cancellation = {});

    [[nodiscard]] CacheMetrics cacheMetrics() const;
    [[nodiscard]] bool setCacheBudget(std::uint64_t bytes);
    void clearUnpinnedCache();

private:
    std::filesystem::path m_plotfile;
    DatasetId m_id;
    PlotfileMetadataResult m_metadataResult;
    PlotfileBlockReader m_blockReader;
    BlockCache m_cache;
};

} // namespace amrvis
