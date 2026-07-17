#include <amrvis/io/StandaloneMetadataReader.hpp>
#include <amrvis/io/detail/VisMfIndex.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amrvis {
namespace {

std::vector<int> parseIntegers(const std::string& text)
{
    std::string numbers = text;
    std::replace_if(numbers.begin(), numbers.end(), [](char character) {
        return !(character >= '0' && character <= '9')
            && character != '-' && character != '+';
    }, ' ');
    std::istringstream input(numbers);
    std::vector<int> result;
    int value = 0;
    while (input >> value) {
        result.push_back(value);
    }
    return result;
}

std::size_t balancedExpressionEnd(const std::string& text, std::size_t start)
{
    if (start >= text.size() || text[start] != '(') {
        throw MetadataReadError("expected a parenthesized FAB header expression");
    }
    int depth = 0;
    for (std::size_t index = start; index < text.size(); ++index) {
        if (text[index] == '(') {
            ++depth;
        } else if (text[index] == ')') {
            --depth;
            if (depth == 0) {
                return index + 1;
            }
        }
    }
    throw MetadataReadError("unterminated FAB header expression");
}

int inferBoxDimension(const std::string& boxText)
{
    const auto innerStart = boxText.find('(', 1);
    const auto innerEnd = boxText.find(')', innerStart);
    if (innerStart == std::string::npos || innerEnd == std::string::npos) {
        throw MetadataReadError("cannot infer the standalone data dimension");
    }
    const auto lower = parseIntegers(
        boxText.substr(innerStart, innerEnd - innerStart + 1));
    if (lower.empty() || lower.size() > 3) {
        throw MetadataReadError("standalone data dimension must be between 1 and 3");
    }
    return static_cast<int>(lower.size());
}

IntBox parseBox(const std::string& boxText, int dimension)
{
    const auto values = parseIntegers(boxText);
    if (values.size() != static_cast<std::size_t>(dimension * 3)) {
        throw MetadataReadError("malformed standalone AMReX Box");
    }
    IntBox box;
    for (int axis = 0; axis < dimension; ++axis) {
        const auto index = static_cast<std::size_t>(axis);
        box.lower[index] = values[index];
        box.upper[index] = values[static_cast<std::size_t>(dimension + axis)];
        box.centering[index] = values[static_cast<std::size_t>(2 * dimension + axis)];
    }
    return box;
}

int inferMultiFabDimension(const std::filesystem::path& headerPath)
{
    std::ifstream input(headerPath);
    if (!input) {
        throw MetadataReadError("cannot open MultiFab header '" + headerPath.string() + "'");
    }
    std::string line;
    while (std::getline(input, line)) {
        const auto boxStart = line.find("((");
        if (boxStart != std::string::npos) {
            return inferBoxDimension(line.substr(boxStart));
        }
    }
    throw MetadataReadError("MultiFab header contains no BoxArray entries");
}

std::shared_ptr<DatasetMetadata> makeSingleLevelMetadata(
    int dimension, const IntBox& domain, int components, std::string_view fieldPrefix)
{
    auto metadata = std::make_shared<DatasetMetadata>();
    metadata->dimension = dimension;
    metadata->finestLevel = 0;
    for (int axis = 0; axis < dimension; ++axis) {
        const auto index = static_cast<std::size_t>(axis);
        metadata->physicalDomain.lower[index] = static_cast<double>(domain.lower[index]);
        metadata->physicalDomain.upper[index] = static_cast<double>(domain.upper[index]) + 1.0;
    }
    metadata->fields.reserve(static_cast<std::size_t>(components));
    for (int component = 0; component < components; ++component) {
        auto name = std::string(fieldPrefix) + std::to_string(component);
        metadata->fields.push_back({name, 1, Centering::Cell, {name}});
    }
    metadata->levels.resize(1);
    metadata->levels.front().domain = domain;
    metadata->levels.front().storedComponents = components;
    return metadata;
}

void validateStandalone(const DatasetMetadata& metadata)
{
    const auto issues = validateMetadata(metadata);
    if (!issues.empty()) {
        throw MetadataReadError("invalid standalone metadata at "
            + issues.front().path + ": " + issues.front().message);
    }
}

} // namespace

PlotfileMetadataResult StandaloneMetadataReader::readFab(
    const std::filesystem::path& fabPath) const
{
    std::ifstream input(fabPath, std::ios::binary);
    if (!input) {
        throw MetadataReadError("cannot open standalone FAB '" + fabPath.string() + "'");
    }
    std::string line;
    if (!std::getline(input, line) || !line.starts_with("FAB ")) {
        throw MetadataReadError("standalone FAB has no supported FAB header");
    }
    const auto descriptorStart = line.find('(', 4);
    const auto descriptorEnd = balancedExpressionEnd(line, descriptorStart);
    const auto boxStart = line.find('(', descriptorEnd);
    const auto boxEnd = balancedExpressionEnd(line, boxStart);
    const auto boxText = line.substr(boxStart, boxEnd - boxStart);
    const auto dimension = inferBoxDimension(boxText);
    const auto box = parseBox(boxText, dimension);
    std::istringstream componentInput(line.substr(boxEnd));
    int components = 0;
    if (!(componentInput >> components) || components <= 0) {
        throw MetadataReadError("standalone FAB component count is invalid");
    }

    auto metadata = makeSingleLevelMetadata(dimension, box, components, "Fab_");
    auto& level = metadata->levels.front();
    level.boxes.push_back(box);
    level.dataPath = fabPath.filename().generic_string();
    level.visMfHeaderVersion = 1;
    level.blocks.push_back({box, fabPath.filename().generic_string(), 0, std::nullopt});
    validateStandalone(*metadata);

    const auto payloadPosition = input.tellg();
    const auto headerBytes = payloadPosition < 0
        ? static_cast<std::uint64_t>(line.size() + 1)
        : static_cast<std::uint64_t>(payloadPosition);
    return {
        std::shared_ptr<const DatasetMetadata>(std::move(metadata)),
        MetadataReadMetrics{1, headerBytes, 0, 0},
        "FAB"
    };
}

PlotfileMetadataResult StandaloneMetadataReader::readMultiFab(
    const std::filesystem::path& prefixOrHeader) const
{
    auto prefix = prefixOrHeader;
    if (prefix.string().ends_with("_H")) {
        auto value = prefix.string();
        value.resize(value.size() - 2);
        prefix = value;
    }
    const auto headerPath = std::filesystem::path(prefix.string() + "_H");
    const auto dimension = inferMultiFabDimension(headerPath);
    const auto index = detail::readVisMfIndex(headerPath, dimension);
    if (index.boxes.empty() || index.components <= 0) {
        throw MetadataReadError("standalone MultiFab is empty");
    }

    auto domain = index.boxes.front();
    for (const auto& box : index.boxes) {
        for (int axis = 0; axis < dimension; ++axis) {
            const auto coordinate = static_cast<std::size_t>(axis);
            domain.lower[coordinate] = std::min(domain.lower[coordinate], box.lower[coordinate]);
            domain.upper[coordinate] = std::max(domain.upper[coordinate], box.upper[coordinate]);
        }
    }
    auto metadata = makeSingleLevelMetadata(
        dimension, domain, index.components, "MultiFab_");
    auto& level = metadata->levels.front();
    level.boxes = index.boxes;
    level.ghostWidth = index.ghostWidth;
    level.dataPath = prefix.filename().generic_string();
    level.visMfHeaderVersion = index.version;
    level.realDescriptor = index.realDescriptor;
    level.blocks.reserve(index.boxes.size());
    for (std::size_t block = 0; block < index.boxes.size(); ++block) {
        BlockMetadata metadataBlock;
        metadataBlock.box = index.boxes[block];
        metadataBlock.filePath = index.fileNames[block];
        metadataBlock.fileOffset = index.fileOffsets[block];
        if (index.hasPerBlockStatistics
            && index.minimum.size() == index.boxes.size()
            && index.maximum.size() == index.boxes.size()) {
            metadataBlock.statistics = BlockStatistics{
                index.minimum[block], index.maximum[block]};
        }
        level.blocks.push_back(std::move(metadataBlock));
    }
    validateStandalone(*metadata);
    return {
        std::shared_ptr<const DatasetMetadata>(std::move(metadata)),
        MetadataReadMetrics{1, index.bytesRead, 0, 0},
        "VisMF-" + std::to_string(index.version)
    };
}

PlotfileMetadataResult readDatasetMetadata(const std::filesystem::path& path)
{
    if (std::filesystem::is_directory(path)
        && std::filesystem::is_regular_file(path / "Header")) {
        return PlotfileMetadataReader{}.read(path);
    }
    if (path.string().ends_with("_H")
        || std::filesystem::is_regular_file(
            std::filesystem::path(path.string() + "_H"))) {
        return StandaloneMetadataReader{}.readMultiFab(path);
    }
    return StandaloneMetadataReader{}.readFab(path);
}

} // namespace amrvis
