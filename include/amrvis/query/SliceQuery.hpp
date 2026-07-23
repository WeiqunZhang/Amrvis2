#pragma once

#include <amrvis/core/Request.hpp>
#include <amrvis/core/Result.hpp>
#include <amrvis/core/StopToken.hpp>
#include <amrvis/io/PlotfileDataset.hpp>

#include <cstdint>

namespace amrvis {

struct SliceQueryMetrics {
    std::uint64_t candidateBlocks = 0;
    std::uint64_t blocksRead = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t payloadBytesRead = 0;
};

struct SliceQueryResult {
    ScalarPlane plane;
    SliceQueryMetrics metrics;
};

class SliceQuery {
public:
    explicit SliceQuery(PlotfileDataset& dataset) : m_dataset(dataset) {}

    [[nodiscard]] SliceQueryResult execute(
        const SliceRequest& request, StopToken cancellation = {});

private:
    PlotfileDataset& m_dataset;
};

} // namespace amrvis
