#pragma once

#include <amrvis/core/Metadata.hpp>
#include <amrvis/core/Request.hpp>
#include <amrvis/core/StopToken.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace amrvis {

struct FabBlock {
    IntBox box;
    FieldId field;
    int component = 0;
    std::vector<double> values;
};

struct BlockReadMetrics {
    std::uint64_t filesRead = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t valuesRead = 0;
};

struct BlockReadResult {
    std::shared_ptr<const FabBlock> block;
    BlockReadMetrics metrics;
};

class BlockReadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ReadCancelled : public std::runtime_error {
public:
    ReadCancelled() : std::runtime_error("block read cancelled") {}
};

class PlotfileBlockReader {
public:
    PlotfileBlockReader(
        std::filesystem::path plotfile, std::shared_ptr<const DatasetMetadata> metadata);

    [[nodiscard]] BlockReadResult readBlock(
        const BlockRequest& request, StopToken cancellation = {}) const;

private:
    std::filesystem::path m_plotfile;
    std::shared_ptr<const DatasetMetadata> m_metadata;
};

} // namespace amrvis
