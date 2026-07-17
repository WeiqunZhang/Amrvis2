#pragma once

#include <amrvis/io/PlotfileMetadataReader.hpp>

#include <filesystem>

namespace amrvis {

class StandaloneMetadataReader {
public:
    [[nodiscard]] PlotfileMetadataResult readFab(
        const std::filesystem::path& fabPath) const;
    [[nodiscard]] PlotfileMetadataResult readMultiFab(
        const std::filesystem::path& prefixOrHeader) const;
};

// Inspect a plotfile directory, standalone FAB, or serialized MultiFab prefix/header.
[[nodiscard]] PlotfileMetadataResult readDatasetMetadata(
    const std::filesystem::path& path);

} // namespace amrvis
