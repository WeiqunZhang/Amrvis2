#include <amrvis/core/Metadata.hpp>
#include <amrvis/core/Request.hpp>
#include <amrvis/core/Statistics.hpp>

#include <cstdlib>
#include <iostream>

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
    amrvis::DatasetMetadata metadata;
    metadata.dimension = 2;
    metadata.finestLevel = 0;
    metadata.physicalDomain = {{{0.0, 0.0, 0.0}}, {{1.0, 1.0, 0.0}}};
    metadata.fields.push_back({"density", 1, amrvis::Centering::Cell, {"density"}});
    amrvis::LevelMetadata level;
    level.domain = {{{0, 0, 0}}, {{3, 3, 0}}, {{0, 0, 0}}};
    level.refinementRatioToNext = {{1, 1, 1}};
    level.cellSize = {{0.25, 0.25, 1.0}};
    level.boxes.push_back(level.domain);
    level.storedComponents = 1;
    level.blocks.push_back({level.domain, "Cell_D_00000", 0,
        amrvis::BlockStatistics{{-2.0}, {7.0}}});
    level.dataPath = "Level_0/Cell";
    metadata.levels.push_back(std::move(level));
    require(amrvis::validateMetadata(metadata).empty(), "valid metadata was rejected");
    const auto range = amrvis::metadataValueRange(metadata, amrvis::FieldId{0});
    require(range && range->minimum == -2.0 && range->maximum == 7.0,
        "metadata range did not aggregate block statistics");
    require(!amrvis::metadataValueRange(metadata, amrvis::FieldId{1}),
        "metadata range accepted an unknown field");

    metadata.fields.push_back(metadata.fields.front());
    require(!amrvis::validateMetadata(metadata).empty(), "duplicate field names were accepted");

    amrvis::BlockRequest block;
    require(!amrvis::validateBlockRequest(block).empty(), "invalid block request was accepted");
    block.dataset.value = 1;
    require(amrvis::validateBlockRequest(block).empty(), "valid block request was rejected");

    amrvis::SliceRequest slice;
    slice.dataset.value = 1;
    slice.normalDirection = 1;
    slice.visibleRegion = {{{0.0, 0.0, 0.0}}, {{1.0, 1.0, 0.0}}};
    slice.outputSize = {640, 480};
    require(amrvis::validateSliceRequest(slice, 2).empty(), "valid slice request was rejected");

    return 0;
}
