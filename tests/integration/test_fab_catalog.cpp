#include <amrvis/io/FabCatalog.hpp>
#include <amrvis/io/PlotfileDataset.hpp>
#include <amrvis/io/StandaloneMetadataReader.hpp>
#include <amrvis/query/SliceQuery.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("amrvis2-fab-catalog-" + std::to_string(unique));
    std::filesystem::create_directories(root);
    const auto path = root / "raw_fabs";

    constexpr std::string_view doubleHeader =
        "FAB ((8, (64 11 52 0 1 12 0 1023)),(8, (8 7 6 5 4 3 2 1)))"
        "((0,0) (1,0) (0,0)) 1\n";
    constexpr std::string_view floatHeader =
        "FAB ((8, (32 8 23 0 1 9 0 127)),(4, (4 3 2 1)))"
        "((2,3) (3,4) (0,1)) 1\n";
    const std::array<double, 2> doubles{1.25, 2.5};
    const std::array<char, 13> padding{};
    const std::array<float, 4> floats{3.0F, 4.0F, 5.0F, 6.0F};
    {
        std::ofstream output(path, std::ios::binary);
        require(static_cast<bool>(output), "could not create raw FAB fixture");
        output.write(doubleHeader.data(),
            static_cast<std::streamsize>(doubleHeader.size()));
        output.write(reinterpret_cast<const char*>(doubles.data()),
            static_cast<std::streamsize>(sizeof(doubles)));
        output.write(padding.data(),
            static_cast<std::streamsize>(padding.size()));
        output.write(floatHeader.data(),
            static_cast<std::streamsize>(floatHeader.size()));
        output.write(reinterpret_cast<const char*>(floats.data()),
            static_cast<std::streamsize>(sizeof(floats)));
    }

    const auto catalog = amrvis::scanFabFile(path);
    require(catalog.size() == 2, "concatenated FAB records were not cataloged");
    require(catalog[0].headerOffset == 0
        && catalog[1].headerOffset
            == doubleHeader.size() + sizeof(doubles) + padding.size(),
        "FAB record offsets are wrong");
    require(catalog[0].precision == amrvis::FabRealPrecision::Double
        && catalog[1].precision == amrvis::FabRealPrecision::Single,
        "FAB precision was not detected");
    require(catalog[1].storedBox.centering[0] == 0
        && catalog[1].storedBox.centering[1] == 1,
        "raw FAB index type was not retained");

    auto selected = amrvis::StandaloneMetadataReader{}.readFab(
        path, catalog[1].headerOffset);
    require(selected.metadata->isFab, "raw FAB metadata was not marked as FAB mode");
    require(selected.metadata->physicalDomain.lower[0] == 2.0
        && selected.metadata->physicalDomain.lower[1] == 2.5,
        "raw FAB sample bounds ignored mixed nodal centering");

    amrvis::PlotfileDataset dataset(
        root, amrvis::DatasetId{1}, 1024U * 1024U, selected);
    amrvis::BlockRequest request;
    request.dataset = dataset.id();
    const auto block = dataset.requestBlock(request);
    require(block.handle->values.precision() == amrvis::FabRealPrecision::Single,
        "selected raw FAB did not retain float storage");
    require(block.handle->values.size() == floats.size()
        && block.handle->values[0] == 3.0
        && block.handle->values[3] == 6.0,
        "selected raw FAB values are wrong");

    amrvis::SliceRequest slice;
    slice.dataset = dataset.id();
    slice.normalDirection = 1;
    slice.visibleRegion = selected.metadata->physicalDomain;
    slice.outputSize = {2, 2};
    const auto plane = amrvis::SliceQuery(dataset).execute(slice).plane;
    require(plane.physicalRegion.lower[0] == 2.0
        && plane.physicalRegion.lower[1] == 2.5,
        "mixed-centering slice region is wrong");
    require(plane.values[0] == 3.0F && plane.values[1] == 4.0F
        && plane.values[2] == 5.0F && plane.values[3] == 6.0F,
        "mixed-centering slice sampled the wrong FAB points");

    amrvis::DatasetMetadata source;
    source.dimension = 2;
    source.finestLevel = 0;
    source.fields.push_back(
        {"value", 1, amrvis::Centering::Cell, {"value"}});
    source.levels.resize(1);
    auto& level = source.levels.front();
    level.domain = {{{0, 0, 0}}, {{1, 1, 0}}, {{0, 0, 0}}};
    level.boxes.push_back(level.domain);
    level.blocks.push_back({level.domain, "raw_fabs", 0, std::nullopt});
    level.ghostWidth = {{1, 2, 0}};
    level.storedComponents = 1;
    level.visMfHeaderVersion = 2;
    level.realDescriptor =
        "(8, (32 8 23 0 1 9 0 127)),(4, (4 3 2 1))";
    const auto fab = amrvis::makeSelectedFabMetadata(source, 0, 0, root);
    require(fab.metadata->levels[0].domain.lower[0] == -1
        && fab.metadata->levels[0].domain.upper[0] == 2
        && fab.metadata->levels[0].domain.lower[1] == -2
        && fab.metadata->levels[0].domain.upper[1] == 3,
        "MultiFab ghost points were not included in selected FAB mode");

    std::filesystem::remove_all(root);
    return 0;
}
