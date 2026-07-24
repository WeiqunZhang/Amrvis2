#pragma once

#include <amrvis/cache/BlockKey.hpp>
#include <amrvis/cache/ByteLruCache.hpp>
#include <amrvis/core/StopToken.hpp>
#include <amrvis/io/PlotfileBlockReader.hpp>
#include <amrvis/io/PlotfileMetadataReader.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace amrvis {

struct DerivedFieldDefinition {
    std::string name;
    std::string expression;
};

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

    // Adds a scalar, cell-centered field whose algebraic expression may
    // reference existing scalar fields by name. The returned id is immediately
    // usable by the ordinary block, slice, and line-query paths.
    [[nodiscard]] FieldId addDerivedField(const DerivedFieldDefinition& definition);
    [[nodiscard]] bool isDerivedField(FieldId field) const noexcept;

    [[nodiscard]] BlockAccess requestBlock(
        const BlockRequest& request, StopToken cancellation = {});

    [[nodiscard]] CacheMetrics cacheMetrics() const;
    [[nodiscard]] bool setCacheBudget(std::uint64_t bytes);
    void clearUnpinnedCache();

private:
    struct DerivedField;

    [[nodiscard]] BlockReadResult readDerivedBlock(
        const BlockRequest& request, const DerivedField& field,
        StopToken cancellation);

    std::filesystem::path m_plotfile;
    DatasetId m_id;
    PlotfileMetadataResult m_metadataResult;
    std::shared_ptr<DatasetMetadata> m_metadata;
    PlotfileBlockReader m_blockReader;
    BlockCache m_cache;
    std::size_t m_storedFieldCount = 0;
    std::vector<std::shared_ptr<const DerivedField>> m_derivedFields;
};

} // namespace amrvis
