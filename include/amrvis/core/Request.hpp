#pragma once

#include <amrvis/core/Geometry.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace amrvis {

struct DatasetId {
    std::uint64_t value = 0;
    auto operator<=>(const DatasetId&) const = default;
};

struct FieldId {
    std::uint32_t value = 0;
    auto operator<=>(const FieldId&) const = default;
};

enum class SamplingPolicy : std::uint8_t {
    Nearest,
    PiecewiseConstant,
    Linear
};

enum class CompositionPolicy : std::uint8_t {
    FinestAvailable,
    ExactLevel
};

struct BlockRequest {
    DatasetId dataset;
    int timestep = 0;
    int level = 0;
    int gridIndex = 0;
    FieldId field;
    int firstComponent = 0;
    int componentCount = 1;
    int ghostWidth = 0;
};

struct SliceRequest {
    DatasetId dataset;
    FieldId field;
    int component = 0;
    int normalDirection = 2;
    double physicalPosition = 0.0;
    RealBox visibleRegion;
    int maximumLevel = 0;
    std::array<int, 2> outputSize{0, 0};
    SamplingPolicy sampling = SamplingPolicy::PiecewiseConstant;
    CompositionPolicy composition = CompositionPolicy::FinestAvailable;
};

[[nodiscard]] std::vector<std::string> validateBlockRequest(const BlockRequest& request);
[[nodiscard]] std::vector<std::string> validateSliceRequest(
    const SliceRequest& request, int datasetDimension);

} // namespace amrvis

