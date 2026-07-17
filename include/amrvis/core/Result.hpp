#pragma once

#include <amrvis/core/Geometry.hpp>

#include <cstdint>
#include <vector>

namespace amrvis {

struct ScalarPlane {
    int width = 0;
    int height = 0;
    RealBox physicalRegion;
    std::vector<float> values;
    std::vector<std::uint8_t> valid;
    std::vector<std::int16_t> sourceLevel;
};

} // namespace amrvis

