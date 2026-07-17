#pragma once

#include <amrvis/core/Geometry.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace amrvis {

enum class Centering : std::uint8_t {
    Cell,
    Node,
    FaceX,
    FaceY,
    FaceZ,
    EdgeX,
    EdgeY,
    EdgeZ,
    Mixed,
    Unknown
};

struct FieldMetadata {
    std::string name;
    int componentCount = 1;
    Centering centering = Centering::Cell;
    std::vector<std::string> componentNames;
};

struct BlockStatistics {
    std::vector<double> minimum;
    std::vector<double> maximum;
};

struct BlockMetadata {
    IntBox box;
    std::string filePath;
    std::uint64_t fileOffset = 0;
    std::optional<BlockStatistics> statistics;
};

struct LevelMetadata {
    int level = 0;
    int step = 0;
    IntBox domain;
    Int3 refinementRatioToNext{{1, 1, 1}};
    Real3 cellSize{{1.0, 1.0, 1.0}};
    Int3 ghostWidth;
    int storedComponents = 0;
    int visMfHeaderVersion = 0;
    std::vector<IntBox> boxes;
    std::vector<BlockMetadata> blocks;
    std::string dataPath;
    std::string realDescriptor;
};

struct DatasetMetadata {
    int dimension = 0;
    int finestLevel = 0;
    double time = 0.0;
    int coordinateSystem = 0;
    RealBox physicalDomain;
    std::vector<LevelMetadata> levels;
    std::vector<FieldMetadata> fields;
};

struct MetadataIssue {
    std::string path;
    std::string message;
};

[[nodiscard]] std::vector<MetadataIssue> validateMetadata(const DatasetMetadata& metadata);

} // namespace amrvis
