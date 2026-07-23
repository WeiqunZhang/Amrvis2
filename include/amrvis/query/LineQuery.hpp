#pragma once

#include <amrvis/core/Request.hpp>
#include <amrvis/core/Result.hpp>
#include <amrvis/core/StopToken.hpp>
#include <amrvis/io/PlotfileDataset.hpp>
#include <amrvis/query/SliceQuery.hpp>

#include <cstdint>

namespace amrvis {

struct LineQueryResult {
    LineResult line;
    SliceQueryMetrics metrics;
};

class LineQuery {
public:
    explicit LineQuery(PlotfileDataset& dataset) : m_dataset(dataset) {}

    [[nodiscard]] LineQueryResult execute(
        const LineRequest& request, StopToken cancellation = {});

private:
    PlotfileDataset& m_dataset;
};

} // namespace amrvis
