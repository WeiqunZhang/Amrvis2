#pragma once

#include <amrvis/core/Metadata.hpp>
#include <amrvis/core/Request.hpp>

#include <optional>

namespace amrvis {

struct ValueRange {
    double minimum = 0.0;
    double maximum = 0.0;
};

// Returns no value when the selected blocks do not carry usable statistics.
[[nodiscard]] std::optional<ValueRange> metadataValueRange(
    const DatasetMetadata& metadata, FieldId field,
    std::optional<int> level = std::nullopt);

} // namespace amrvis
