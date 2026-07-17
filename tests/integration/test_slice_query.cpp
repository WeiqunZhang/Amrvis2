#include <amrvis/query/SliceQuery.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view realDescriptor =
    "((8, (64 11 52 0 1 12 0 1023)),(8, (8 7 6 5 4 3 2 1)))";

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "could not create slice fixture text");
    output << text;
}

void writeFab(const std::filesystem::path& path, std::string_view box,
    const std::array<double, 16>& values)
{
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "could not create slice fixture FAB");
    output << "FAB " << realDescriptor << box << " 1\n";
    output.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(sizeof(values)));
}

} // namespace

int main()
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("amrvis2-slice-query-" + std::to_string(unique));
    std::filesystem::create_directories(root / "Level_0");
    std::filesystem::create_directories(root / "Level_1");

    writeText(root / "Header",
        "HyperCLaw-V1.1\n"
        "1\nphi\n"
        "2\n0.0\n1\n"
        "0.0 0.0\n1.0 1.0\n2\n"
        "((0,0) (3,3) (0,0))\n"
        "((0,0) (7,7) (0,0))\n"
        "0 0\n"
        "0.25 0.25\n0.125 0.125\n"
        "0\n0\n"
        "0 1 0.0\n0\n"
        "0.0 1.0\n0.0 1.0\n"
        "Level_0/Cell\n"
        "1 1 0.0\n0\n"
        "0.25 0.75\n0.25 0.75\n"
        "Level_1/Cell\n");
    writeText(root / "Level_0" / "Cell_H",
        "1\n1\n1\n0\n"
        "(1 0\n((0,0) (3,3) (0,0))\n)\n"
        "1\nFabOnDisk: Cell_D_00000 0\n\n"
        "1,1\n1.0,\n\n1,1\n1.0,\n\n");
    writeText(root / "Level_1" / "Cell_H",
        "1\n1\n1\n0\n"
        "(1 0\n((2,2) (5,5) (0,0))\n)\n"
        "1\nFabOnDisk: Cell_D_00000 0\n\n"
        "1,1\n2.0,\n\n1,1\n2.0,\n\n");

    std::array<double, 16> coarse{};
    std::array<double, 16> fine{};
    coarse.fill(1.0);
    fine.fill(2.0);
    writeFab(root / "Level_0" / "Cell_D_00000",
        "((0,0) (3,3) (0,0))", coarse);
    writeFab(root / "Level_1" / "Cell_D_00000",
        "((2,2) (5,5) (0,0))", fine);

    amrvis::PlotfileDataset dataset(root, amrvis::DatasetId{9}, 1024 * 1024);
    amrvis::SliceRequest request;
    request.dataset.value = 9;
    request.field.value = 0;
    request.normalDirection = 1;
    request.visibleRegion = {{{0.0, 0.0, 0.0}}, {{1.0, 1.0, 0.0}}};
    request.maximumLevel = 1;
    request.outputSize = {4, 4};

    amrvis::SliceQuery query(dataset);
    const auto composite = query.execute(request);
    require(composite.metrics.blocksRead == 2, "slice did not read the two intersecting blocks");
    require(composite.metrics.cacheHits == 0, "first slice unexpectedly hit cache");
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const auto offset = static_cast<std::size_t>(x + 4 * y);
            const bool coveredByFine = x >= 1 && x <= 2 && y >= 1 && y <= 2;
            require(composite.plane.valid[offset] == 1, "composite slice left a coarse hole");
            require(composite.plane.values[offset] == (coveredByFine ? 2.0F : 1.0F),
                "fine-over-coarse value mismatch");
            require(composite.plane.sourceLevel[offset] == (coveredByFine ? 1 : 0),
                "source-level mask mismatch");
        }
    }

    const auto cached = query.execute(request);
    require(cached.metrics.blocksRead == 0 && cached.metrics.cacheHits == 2,
        "repeated slice did not reuse both blocks");
    require(cached.metrics.payloadBytesRead == 0, "cached slice performed payload I/O");

    request.composition = amrvis::CompositionPolicy::ExactLevel;
    const auto exact = query.execute(request);
    require(exact.plane.valid[0] == 0, "exact-level slice filled a fine-level hole");
    require(exact.plane.valid[5] == 1 && exact.plane.values[5] == 2.0F,
        "exact-level slice missed fine data");

    std::filesystem::remove_all(root);
    return 0;
}

