#include <amrvis/io/PlotfileDataset.hpp>
#include <amrvis/io/StandaloneMetadataReader.hpp>

#if AMRVIS_ENABLE_DERIVED_FIELDS
#include <amrexpr.hpp>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace amrvis {
namespace {

std::mutex& globalAmrexIoMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::uint64_t residentBytes(const FabBlock& block)
{
    return static_cast<std::uint64_t>(sizeof(FabBlock))
        + static_cast<std::uint64_t>(block.values.capacity()) * sizeof(double);
}

std::filesystem::path dataRoot(const std::filesystem::path& path)
{
    if (std::filesystem::is_directory(path)
        && std::filesystem::is_regular_file(path / "Header")) {
        return path;
    }
    const auto parent = path.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

#if AMRVIS_ENABLE_DERIVED_FIELDS

constexpr std::size_t maximumDerivedInputs = 16;
using DerivedEvaluator = std::function<double(std::span<const double>)>;

template <std::size_t N>
DerivedEvaluator makeEvaluator(const std::shared_ptr<amrexpr::Parser>& parser)
{
    const auto executor = parser->compileHost<static_cast<int>(N)>();
    return [executor](std::span<const double> values) {
        if (values.size() != N) {
            throw std::logic_error("derived-field parser input count changed");
        }
        std::array<double, N> arguments{};
        std::copy(values.begin(), values.end(), arguments.begin());
        return std::apply(executor, arguments);
    };
}

template <std::size_t N = 0>
DerivedEvaluator compileEvaluator(
    const std::shared_ptr<amrexpr::Parser>& parser, std::size_t inputCount)
{
    if (inputCount == N) {
        return makeEvaluator<N>(parser);
    }
    if constexpr (N < maximumDerivedInputs) {
        return compileEvaluator<N + 1>(parser, inputCount);
    } else {
        throw std::invalid_argument(
            "derived-field expressions may reference at most "
            + std::to_string(maximumDerivedInputs) + " fields");
    }
}

std::uint64_t pointCount(const IntBox& box, int dimension)
{
    std::uint64_t result = 1;
    for (int axis = 0; axis < dimension; ++axis) {
        const auto i = static_cast<std::size_t>(axis);
        const auto signedLength = static_cast<std::int64_t>(box.upper[i])
            - box.lower[i] + 1;
        if (signedLength <= 0) {
            throw BlockReadError("derived-field block has a non-positive extent");
        }
        const auto length = static_cast<std::uint64_t>(signedLength);
        if (result > std::numeric_limits<std::uint64_t>::max() / length) {
            throw BlockReadError("derived-field point count overflows");
        }
        result *= length;
    }
    return result;
}

#endif

} // namespace

struct PlotfileDataset::DerivedField {
#if AMRVIS_ENABLE_DERIVED_FIELDS
    std::string expression;
    std::vector<FieldId> inputs;
    std::shared_ptr<amrexpr::Parser> parser;
    DerivedEvaluator evaluate;
#endif
};

PlotfileDataset::PlotfileDataset(
    std::filesystem::path plotfile, DatasetId id, std::uint64_t cacheBudgetBytes)
    : m_plotfile(dataRoot(plotfile))
    , m_id(id)
    , m_metadataResult(readDatasetMetadata(plotfile))
    , m_metadata(std::make_shared<DatasetMetadata>(*m_metadataResult.metadata))
    , m_blockReader(m_plotfile, m_metadata)
    , m_cache(cacheBudgetBytes)
    , m_storedFieldCount(m_metadata->fields.size())
{
    if (m_id.value == 0) {
        throw std::invalid_argument("PlotfileDataset id must be nonzero");
    }
}

const DatasetMetadata& PlotfileDataset::metadata() const noexcept
{
    return *m_metadata;
}

const MetadataReadMetrics& PlotfileDataset::metadataReadMetrics() const noexcept
{
    return m_metadataResult.metrics;
}

DatasetId PlotfileDataset::id() const noexcept
{
    return m_id;
}

FieldId PlotfileDataset::addDerivedField(
    const DerivedFieldDefinition& definition)
{
#if AMRVIS_ENABLE_DERIVED_FIELDS
    if (definition.name.empty()) {
        throw std::invalid_argument("derived-field name must not be empty");
    }
    if (definition.expression.empty()) {
        throw std::invalid_argument("derived-field expression must not be empty");
    }
    const auto duplicate = std::find_if(
        m_metadata->fields.begin(), m_metadata->fields.end(),
        [&definition](const FieldMetadata& field) {
            return field.name == definition.name;
        });
    if (duplicate != m_metadata->fields.end()) {
        throw std::invalid_argument(
            "field name '" + definition.name + "' is already in use");
    }

    auto derived = std::make_shared<DerivedField>();
    derived->expression = definition.expression;
    derived->parser = std::make_shared<amrexpr::Parser>(definition.expression);

    const auto symbols = derived->parser->symbols();
    std::vector<std::string> variables;
    variables.reserve(symbols.size());
    derived->inputs.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        const auto input = std::find_if(
            m_metadata->fields.begin(), m_metadata->fields.end(),
            [&symbol](const FieldMetadata& field) {
                return field.name == symbol;
            });
        if (input == m_metadata->fields.end()) {
            throw std::invalid_argument(
                "unknown field '" + symbol + "' in derived-field expression");
        }
        if (input->componentCount != 1) {
            throw std::invalid_argument(
                "derived-field inputs must be scalar fields");
        }
        const auto index = static_cast<std::size_t>(
            std::distance(m_metadata->fields.begin(), input));
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("derived-field id exceeds supported range");
        }
        variables.push_back(symbol);
        derived->inputs.push_back(
            FieldId{static_cast<std::uint32_t>(index)});
    }
    derived->parser->registerVariables(variables);
    derived->evaluate = compileEvaluator(
        derived->parser, static_cast<std::size_t>(variables.size()));

    if (m_metadata->fields.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("derived-field id exceeds supported range");
    }
    const auto id = FieldId{
        static_cast<std::uint32_t>(m_metadata->fields.size())};
    m_metadata->fields.push_back(FieldMetadata{
        .name = definition.name,
        .componentCount = 1,
        .centering = Centering::Cell,
        .componentNames = {}
    });
    m_derivedFields.push_back(std::move(derived));
    return id;
#else
    static_cast<void>(definition);
    throw std::logic_error(
        "derived fields are unavailable in this build");
#endif
}

bool PlotfileDataset::isDerivedField(FieldId field) const noexcept
{
#if AMRVIS_ENABLE_DERIVED_FIELDS
    return field.value >= m_storedFieldCount
        && static_cast<std::size_t>(field.value) < m_metadata->fields.size();
#else
    static_cast<void>(field);
    return false;
#endif
}

PlotfileDataset::BlockAccess PlotfileDataset::requestBlock(
    const BlockRequest& request, std::stop_token cancellation)
{
    if (request.dataset != m_id) {
        throw BlockReadError("block request targets a different dataset");
    }
    const auto key = makeBlockKey(request);
    if (auto cached = m_cache.findAndPin(key)) {
        return {std::move(cached), true, {}};
    }

    if (request.field.value >= m_metadata->fields.size()) {
        throw BlockReadError("requested field is unavailable");
    }
    if (isDerivedField(request.field)) {
        const auto derivedIndex =
            static_cast<std::size_t>(request.field.value) - m_storedFieldCount;
        auto read = readDerivedBlock(
            request, *m_derivedFields.at(derivedIndex), cancellation);
        auto handle = m_cache.insertAndPin(
            key, read.block, residentBytes(*read.block));
        return {std::move(handle), false, read.metrics};
    }

    std::scoped_lock ioLock(globalAmrexIoMutex());
    if (cancellation.stop_requested()) {
        throw ReadCancelled();
    }
    if (auto cached = m_cache.findAndPin(key)) {
        return {std::move(cached), true, {}};
    }

    auto read = m_blockReader.readBlock(request, cancellation);
    auto handle = m_cache.insertAndPin(
        key, read.block, residentBytes(*read.block));
    return {std::move(handle), false, read.metrics};
}

BlockReadResult PlotfileDataset::readDerivedBlock(
    const BlockRequest& request, const DerivedField& field,
    std::stop_token cancellation)
{
#if AMRVIS_ENABLE_DERIVED_FIELDS
    if (request.componentCount != 1 || request.firstComponent != 0) {
        throw BlockReadError("derived fields are scalar");
    }
    if (request.level < 0
        || static_cast<std::size_t>(request.level) >= m_metadata->levels.size()) {
        throw BlockReadError("requested level is unavailable");
    }
    const auto& level = m_metadata->levels[static_cast<std::size_t>(request.level)];
    if (request.gridIndex < 0
        || static_cast<std::size_t>(request.gridIndex) >= level.blocks.size()) {
        throw BlockReadError("requested grid is unavailable");
    }

    std::vector<BlockCache::Handle> inputBlocks;
    inputBlocks.reserve(field.inputs.size());
    BlockReadMetrics metrics;
    for (const auto inputField : field.inputs) {
        auto inputRequest = request;
        inputRequest.field = inputField;
        auto access = requestBlock(inputRequest, cancellation);
        metrics.filesRead += access.io.filesRead;
        metrics.bytesRead += access.io.bytesRead;
        metrics.valuesRead += access.io.valuesRead;
        inputBlocks.push_back(std::move(access.handle));
    }

    auto block = std::make_shared<FabBlock>();
    block->box = inputBlocks.empty()
        ? level.blocks[static_cast<std::size_t>(request.gridIndex)].box
        : inputBlocks.front()->box;
    block->field = request.field;
    block->component = 0;
    const auto count = pointCount(block->box, m_metadata->dimension);
    if (count > std::numeric_limits<std::size_t>::max()) {
        throw BlockReadError("derived-field block exceeds addressable memory");
    }
    block->values.resize(static_cast<std::size_t>(count));

    for (const auto& input : inputBlocks) {
        if (!(input->box == block->box)
            || input->values.size() != block->values.size()) {
            throw BlockReadError(
                "derived-field inputs do not share the same cell box");
        }
    }
    std::vector<double> arguments(inputBlocks.size());
    for (std::size_t cell = 0; cell < block->values.size(); ++cell) {
        if ((cell & 4095U) == 0U && cancellation.stop_requested()) {
            throw ReadCancelled();
        }
        for (std::size_t input = 0; input < inputBlocks.size(); ++input) {
            arguments[input] = inputBlocks[input]->values[cell];
        }
        block->values[cell] = field.evaluate(arguments);
    }
    return {std::shared_ptr<const FabBlock>(std::move(block)), metrics};
#else
    static_cast<void>(request);
    static_cast<void>(field);
    static_cast<void>(cancellation);
    throw BlockReadError("derived fields are unavailable in this build");
#endif
}

CacheMetrics PlotfileDataset::cacheMetrics() const
{
    return m_cache.metrics();
}

bool PlotfileDataset::setCacheBudget(std::uint64_t bytes)
{
    return m_cache.setBudget(bytes);
}

void PlotfileDataset::clearUnpinnedCache()
{
    m_cache.clearUnpinned();
}

} // namespace amrvis
