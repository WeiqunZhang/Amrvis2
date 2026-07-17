#include <amrvis/core/Metadata.hpp>

#include <cstddef>
#include <string>
#include <unordered_set>

namespace amrvis {

std::vector<MetadataIssue> validateMetadata(const DatasetMetadata& metadata)
{
    std::vector<MetadataIssue> issues;
    const auto add = [&issues](std::string path, std::string message) {
        issues.push_back({std::move(path), std::move(message)});
    };

    if (metadata.dimension < 1 || metadata.dimension > 3) {
        add("dimension", "must be between 1 and 3");
    }
    if (metadata.finestLevel < 0) {
        add("finestLevel", "must be non-negative");
    }
    if (!metadata.physicalDomain.valid(metadata.dimension)) {
        add("physicalDomain", "must have finite positive extent in every active direction");
    }

    const auto expectedLevels = metadata.finestLevel >= 0
        ? static_cast<std::size_t>(metadata.finestLevel + 1)
        : std::size_t{0};
    if (metadata.levels.size() != expectedLevels) {
        add("levels", "size must equal finestLevel + 1");
    }

    std::unordered_set<std::string> fieldNames;
    for (std::size_t fieldIndex = 0; fieldIndex < metadata.fields.size(); ++fieldIndex) {
        const auto& field = metadata.fields[fieldIndex];
        const auto path = "fields[" + std::to_string(fieldIndex) + "]";
        if (field.name.empty()) {
            add(path + ".name", "must not be empty");
        } else if (!fieldNames.insert(field.name).second) {
            add(path + ".name", "must be unique");
        }
        if (field.componentCount <= 0) {
            add(path + ".componentCount", "must be positive");
        }
        if (!field.componentNames.empty()
            && field.componentNames.size() != static_cast<std::size_t>(field.componentCount)) {
            add(path + ".componentNames", "size must equal componentCount when provided");
        }
    }

    for (std::size_t levelIndex = 0; levelIndex < metadata.levels.size(); ++levelIndex) {
        const auto& level = metadata.levels[levelIndex];
        const auto path = "levels[" + std::to_string(levelIndex) + "]";
        if (level.level != static_cast<int>(levelIndex)) {
            add(path + ".level", "must match its index");
        }
        if (!level.domain.valid(metadata.dimension)) {
            add(path + ".domain", "must be a valid index-space box");
        }
        for (int axis = 0; axis < metadata.dimension; ++axis) {
            const auto i = static_cast<std::size_t>(axis);
            if (!(level.cellSize[i] > 0.0)) {
                add(path + ".cellSize", "must be positive in every active direction");
                break;
            }
            if (levelIndex + 1 < metadata.levels.size()
                && level.refinementRatioToNext[i] <= 0) {
                add(path + ".refinementRatioToNext",
                    "must be positive in every active direction");
                break;
            }
        }
        for (std::size_t boxIndex = 0; boxIndex < level.boxes.size(); ++boxIndex) {
            const auto& box = level.boxes[boxIndex];
            const auto boxPath = path + ".boxes[" + std::to_string(boxIndex) + "]";
            if (!box.valid(metadata.dimension)) {
                add(boxPath, "must be a valid index-space box");
                continue;
            }
            for (int axis = 0; axis < metadata.dimension; ++axis) {
                const auto i = static_cast<std::size_t>(axis);
                if (box.lower[i] < level.domain.lower[i]
                    || box.upper[i] > level.domain.upper[i]) {
                    add(boxPath, "must lie within the level domain");
                    break;
                }
                if (box.centering[i] != level.domain.centering[i]) {
                    add(boxPath + ".centering", "must match the level domain");
                    break;
                }
            }
        }
        if (!level.blocks.empty() && level.blocks.size() != level.boxes.size()) {
            add(path + ".blocks", "size must match the level box array");
        }
        for (std::size_t blockIndex = 0; blockIndex < level.blocks.size(); ++blockIndex) {
            const auto& block = level.blocks[blockIndex];
            const auto blockPath = path + ".blocks[" + std::to_string(blockIndex) + "]";
            if (!(block.box == level.boxes[blockIndex])) {
                add(blockPath + ".box", "must match the corresponding level box");
            }
            if (block.filePath.empty()) {
                add(blockPath + ".filePath", "must not be empty");
            }
            if (block.statistics) {
                const auto expectedComponents = static_cast<std::size_t>(level.storedComponents);
                if (block.statistics->minimum.size() != expectedComponents
                    || block.statistics->maximum.size() != expectedComponents) {
                    add(blockPath + ".statistics",
                        "component count must match the stored data");
                }
            }
        }
    }

    return issues;
}

} // namespace amrvis
