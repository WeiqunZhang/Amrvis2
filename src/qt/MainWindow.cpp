#include "MainWindow.hpp"
#include "AnimationPanel.hpp"
#include "ColorBarWidget.hpp"
#include "DatasetWindow.hpp"
#include "ImageView.hpp"
#include "IsoWidget.hpp"
#include "LinePlotRequest.hpp"
#include "LinePlotWindow.hpp"
#include "SetContoursDialog.hpp"

#include <amrvis/io/PlotfileDataset.hpp>
#include <amrvis/io/StandaloneMetadataReader.hpp>
#include <amrvis/core/Statistics.hpp>
#include <amrvis/query/LineQuery.hpp>
#include <amrvis/query/SliceQuery.hpp>
#include <amrvis/render2d/Contours.hpp>
#include <amrvis/render2d/Palette.hpp>
#include <amrvis/render2d/ScalarRenderer.hpp>

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QException>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QtDebug>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Fed from the project version through a CMake compile definition; the
// fallback covers builds that do not set it (e.g. some IDE integrations).
#ifndef AMRVIS_VERSION
#define AMRVIS_VERSION "0.1.0-dev"
#endif

namespace amrvis::qt {
namespace {

constexpr std::uint64_t initialCacheBudget = 256ULL * 1024ULL * 1024ULL;

// Sequence frame loads and prefetches get dataset ids from a dedicated range
// so they never collide with the ids openDataset derives from m_generation.
constexpr std::uint64_t sequenceDatasetIdBase = 0x4000000000000000ULL;

constexpr std::array<BuiltinPalette, 5> builtinPalettes{
    BuiltinPalette::Rainbow, BuiltinPalette::RedWhiteBlue, BuiltinPalette::Viridis,
    BuiltinPalette::Vort, BuiltinPalette::BlueFlame};
constexpr std::array<const char*, 5> builtinPaletteNames{
    "Rainbow", "RedWhiteBlue", "Viridis", "Vort", "BlueFlame"};

QSettings makeSettings()
{
    return QSettings(QStringLiteral("Amrvis2"), QStringLiteral("Amrvis2"));
}

// Qt Concurrent masks worker exceptions behind QUnhandledException, so the
// underlying library error text must be unwrapped before it is shown.
QString exceptionMessage(const std::exception& error)
{
    const auto* unhandled = dynamic_cast<const QUnhandledException*>(&error);
    if (unhandled != nullptr && unhandled->exception()) {
        try {
            std::rethrow_exception(unhandled->exception());
        } catch (const std::exception& inner) {
            return QString::fromUtf8(inner.what());
        } catch (...) {
            return QStringLiteral("unknown non-std exception");
        }
    }
    return QString::fromUtf8(error.what());
}

std::pair<double, double> finiteRange(const ScalarPlane& plane, bool positiveOnly)
{
    auto minimum = std::numeric_limits<double>::infinity();
    auto maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t pixel = 0; pixel < plane.values.size(); ++pixel) {
        if (plane.valid[pixel] == 0) {
            continue;
        }
        const auto value = static_cast<double>(plane.values[pixel]);
        if (!std::isfinite(value) || (positiveOnly && !(value > 0.0))) {
            continue;
        }
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        throw std::runtime_error("slice contains no finite field values");
    }
    if (minimum == maximum) {
        const auto padding = std::max(std::abs(minimum), 1.0) * 1.0e-6;
        minimum -= padding;
        maximum += padding;
    }
    return {minimum, maximum};
}

// The display range for a slice: the user's explicit range, the level/file
// metadata range, or the finite extrema of the plane itself (positive
// extrema for a logarithmic scale), padded so minimum < maximum always
// holds. Shared by executeSlice and the re-render-from-cache path, which
// must agree exactly.
std::pair<double, double> resolveRange(
    const std::shared_ptr<PlotfileDataset>& dataset, FieldId field,
    int maximumLevel, RangeMode rangeMode,
    const std::optional<std::pair<double, double>>& userRange,
    bool logarithmic, const ScalarPlane& plane)
{
    auto selectedRange = userRange;
    if (rangeMode == RangeMode::Level || rangeMode == RangeMode::File) {
        const auto statistics = metadataValueRange(dataset->metadata(), field,
            rangeMode == RangeMode::Level
                ? std::optional<int>(maximumLevel) : std::nullopt);
        if (!statistics) {
            throw std::runtime_error(
                "the selected dataset does not provide this metadata range");
        }
        selectedRange = std::pair{statistics->minimum, statistics->maximum};
    }
    auto [minimum, maximum] = selectedRange
        ? *selectedRange : finiteRange(plane, logarithmic);
    if (minimum == maximum) {
        if (logarithmic && minimum > 0.0) {
            minimum /= 1.0 + 1.0e-6;
            maximum *= 1.0 + 1.0e-6;
        } else {
            const auto padding = std::max(std::abs(minimum), 1.0) * 1.0e-6;
            minimum -= padding;
            maximum += padding;
        }
    }
    if (!(minimum < maximum)) {
        throw std::runtime_error("user scalar range must have positive extent");
    }
    if (logarithmic && !(minimum > 0.0)) {
        throw std::runtime_error("logarithmic scalar range must be positive");
    }
    return {minimum, maximum};
}

// Native render resolution for a slice: the count of finest-level cells the
// visible region spans along each in-plane axis. At the 1x fixed scale this is
// the resolution legacy Amrvis drew (one pixel per finest cell); the larger
// fixed scales magnify it through the view zoom.
std::array<int, 2> finestNativeOutputSize(
    const amrvis::DatasetMetadata& metadata, const amrvis::RealBox& region,
    int normal)
{
    const auto& finest = metadata.levels[static_cast<std::size_t>(
        std::max(0, metadata.finestLevel))];
    std::array<int, 2> axes{0, 1};
    if (metadata.dimension == 3) {
        std::size_t next = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (axis != normal) {
                axes[next++] = axis;
            }
        }
    }
    const auto cells = [&](int axis) {
        const auto i = static_cast<std::size_t>(axis);
        const auto extent = region.upper[i] - region.lower[i];
        return std::clamp(
            static_cast<int>(std::max(1.0, std::round(extent / finest.cellSize[i]))),
            1, 2048);
    };
    return {cells(axes[0]), cells(axes[1])};
}

// Like resolveRange, but if a logarithmic scale is requested and the range
// cannot be made positive (no positive values, or a Level/File/User minimum
// <= 0), it falls back to a linear range and reports logarithmic=false so the
// caller renders linearly instead of failing the whole slice.
struct ResolvedRange {
    double minimum;
    double maximum;
    bool logarithmic;
};

ResolvedRange resolveDisplayRange(
    const std::shared_ptr<PlotfileDataset>& dataset, FieldId field,
    int maximumLevel, RangeMode rangeMode,
    const std::optional<std::pair<double, double>>& userRange,
    bool logarithmic, const ScalarPlane& plane)
{
    if (logarithmic) {
        try {
            const auto [minimum, maximum] = resolveRange(dataset, field,
                maximumLevel, rangeMode, userRange, true, plane);
            return {minimum, maximum, true};
        } catch (const std::exception&) {
            // Log is not viable for this range; fall back to linear below.
        }
    }
    const auto [minimum, maximum] = resolveRange(dataset, field, maximumLevel,
        rangeMode, userRange, false, plane);
    return {minimum, maximum, false};
}

SliceDisplayResult executeSlice(const std::shared_ptr<PlotfileDataset>& dataset,
    const SliceRequest& request,
    RangeMode rangeMode,
    const std::optional<std::pair<double, double>>& userRange,
    bool logarithmic, const Palette& palette, std::stop_token cancellation)
{
    SliceDisplayResult result;
    result.request = request;
    result.slice = SliceQuery(*dataset).execute(request, cancellation);
    const auto range = resolveDisplayRange(dataset, request.field,
        request.maximumLevel, rangeMode, userRange, logarithmic,
        result.slice.plane);
    result.minimum = range.minimum;
    result.maximum = range.maximum;
    result.logarithmic = range.logarithmic;
    result.fieldName = dataset->metadata().fields[request.field.value].name;
    result.image = renderScalarPlane(result.slice.plane,
        ScalarRenderSettings{
            .minimum = range.minimum,
            .maximum = range.maximum,
            .logarithmic = range.logarithmic,
            .palette = &palette
        });
    return result;
}

// Vector mode renders the U component's raster and derives arrow glyphs from
// both component planes; the V slice shares the U request's region, level,
// and output size so the two planes line up sample for sample.
void appendVectorGlyphs(const std::shared_ptr<PlotfileDataset>& dataset,
    SliceRequest request, FieldId vField, int count,
    std::stop_token cancellation, SliceDisplayResult& result)
{
    request.field = vField;
    auto vSlice = SliceQuery(*dataset).execute(request, cancellation);
    result.vectors = generateVectorGlyphs(result.slice.plane, vSlice.plane, count);
    result.slice.metrics.candidateBlocks += vSlice.metrics.candidateBlocks;
    result.slice.metrics.blocksRead += vSlice.metrics.blocksRead;
    result.slice.metrics.cacheHits += vSlice.metrics.cacheHits;
    result.slice.metrics.payloadBytesRead += vSlice.metrics.payloadBytesRead;
}

[[nodiscard]] bool isContourMode(DisplayMode mode)
{
    return mode == DisplayMode::RasterContours
        || mode == DisplayMode::ColorContours || mode == DisplayMode::BWContours;
}

// The cache-key comparison for PlaneViewState: everything a cached slice
// depends on. Range, log scale, palette, and contour count are deliberately
// absent — those are recomputed from the cached planes on the cheap path.
[[nodiscard]] bool sameSliceSpec(const SliceRequest& lhs, const SliceRequest& rhs)
{
    return lhs.dataset == rhs.dataset && lhs.field == rhs.field
        && lhs.component == rhs.component
        && lhs.normalDirection == rhs.normalDirection
        && lhs.physicalPosition == rhs.physicalPosition
        && lhs.visibleRegion == rhs.visibleRegion
        && lhs.maximumLevel == rhs.maximumLevel
        && lhs.outputSize == rhs.outputSize
        && lhs.sampling == rhs.sampling
        && lhs.composition == rhs.composition;
}

// The two in-plane axes of a slice, mirroring SliceQuery's plane axes.
[[nodiscard]] std::array<int, 2> slicePlaneAxes(int dimension, int normalDirection)
{
    if (dimension == 2) {
        return {0, 1};
    }
    std::array<int, 2> axes{};
    std::size_t next = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis != normalDirection) {
            axes[next++] = axis;
        }
    }
    return axes;
}

// The number of cells of `level` covering [lower, upper] on `axis`, clipped
// to the level's index domain: the data resolution of a slice request.
[[nodiscard]] int coveredCells(const DatasetMetadata& metadata, int level,
    int axis, double lower, double upper)
{
    const auto& levelMetadata = metadata.levels[static_cast<std::size_t>(level)];
    const auto index = static_cast<std::size_t>(axis);
    const auto lo = std::max(lower, metadata.physicalDomain.lower[index]);
    const auto hi = std::min(upper, metadata.physicalDomain.upper[index]);
    if (!(lo < hi)) {
        return 1;
    }
    const auto origin = metadata.physicalDomain.lower[index];
    const auto cellSize = levelMetadata.cellSize[index];
    const auto domainCells = static_cast<std::int64_t>(
        levelMetadata.domain.upper[index]) - levelMetadata.domain.lower[index];
    const auto first = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor((lo - origin) / cellSize)),
        0, domainCells);
    const auto last = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor(
            (std::nextafter(hi, lo) - origin) / cellSize)),
        0, domainCells);
    return static_cast<int>(last - first + 1);
}

// Contour overlays are extracted from a piecewise-constant slice at DATA
// resolution: one sample per cell of the finest participating level covering
// the visible region, capped at 1024 samples per axis (for coarse data this
// plane is tiny — 4x4 on the test fixture). When the data is finer than the
// display on both axes the display plane doubles as the contour plane,
// because the cell-scale staircase is sub-pixel there. The contour plane is
// then bilinearly refined so one fine cell spans at most a few display
// pixels; marching squares on the refinement converges to the exact
// iso-curve of the bilinear interpolant (no new extrema, no ringing), and a
// single Chaikin pass finishes the polyline. This replaces the old second
// SliceQuery with linear sampling at display resolution, which cost about as
// much as the display query itself. Extraction runs here, off the GUI
// thread, with the output mapped to display-plane pixel space; the GUI
// thread only converts the polylines to painter paths in updateOverlay. The
// planes and the refinement are cached in PlaneViewState, so range and
// contour-count changes re-run only this cheap extraction (see
// refreshCachedSlice).
void appendContours(const std::shared_ptr<PlotfileDataset>& dataset,
    const SliceRequest& request, int contourCount, double minimum, double maximum,
    std::stop_token cancellation, SliceDisplayResult& result)
{
    const auto& metadata = dataset->metadata();
    const auto level = std::min(request.maximumLevel, metadata.finestLevel);
    const auto axes = slicePlaneAxes(metadata.dimension, request.normalDirection);
    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto dataWidth = coveredCells(metadata, level, axes[0],
        request.visibleRegion.lower[xAxis], request.visibleRegion.upper[xAxis]);
    const auto dataHeight = coveredCells(metadata, level, axes[1],
        request.visibleRegion.lower[yAxis], request.visibleRegion.upper[yAxis]);
    const auto displayWidth = request.outputSize[0];
    const auto displayHeight = request.outputSize[1];

    if (dataWidth >= displayWidth && dataHeight >= displayHeight) {
        result.contourPlane = result.slice.plane;
    } else {
        auto contourRequest = request;
        contourRequest.outputSize = {
            std::min(dataWidth, 1024), std::min(dataHeight, 1024)};
        contourRequest.sampling = SamplingPolicy::PiecewiseConstant;
        auto contour = SliceQuery(*dataset).execute(contourRequest, cancellation);
        result.contourPlane = std::move(contour.plane);
        result.slice.metrics.candidateBlocks += contour.metrics.candidateBlocks;
        result.slice.metrics.blocksRead += contour.metrics.blocksRead;
        result.slice.metrics.cacheHits += contour.metrics.cacheHits;
        result.slice.metrics.payloadBytesRead += contour.metrics.payloadBytesRead;
    }

    const auto factor = contourUpsampleFactor(result.contourPlane.width,
        result.contourPlane.height, displayWidth, displayHeight);
    result.contourFinePlane = supersamplePlane(result.contourPlane, factor);
    result.contourFineFactor = factor;
    const auto values = contourValues(minimum, maximum, contourCount);
    result.contourPolylines = contourPolylinesForDisplay(
        result.contourFinePlane, factor, values, displayWidth, displayHeight);
}

// Re-render-from-cache: only palette/log/range/contour-count cosmetics
// changed (the request still matches the view's cache key), so the cached
// planes are re-ranged, re-rendered, and re-contoured without any SliceQuery.
// With rasterDirty false the raster is known unchanged and the image is not
// re-rendered; SliceDisplayResult::rasterUnchanged tells showSlice to keep
// the view's pixmap. Vector glyphs are reused from the cache: they do not
// depend on palette/log/range. Runs on a worker; delivery uses the same
// watcher and generation flow as a full slice request.
SliceDisplayResult refreshCachedSlice(
    const std::shared_ptr<PlotfileDataset>& dataset,
    const SliceRequest& request, ScalarPlane displayPlane,
    ScalarPlane contourPlane, ScalarPlane contourFinePlane, int contourFineFactor,
    std::vector<VectorSegment> vectors,
    RangeMode rangeMode,
    const std::optional<std::pair<double, double>>& userRange,
    bool logarithmic, const Palette& palette, DisplayMode displayMode,
    std::uint32_t vectorVField, int contourCount, bool rasterDirty)
{
    SliceDisplayResult result;
    result.request = request;
    result.mode = displayMode;
    result.vectorVField = vectorVField;
    result.contourCount = contourCount;
    result.slice.plane = std::move(displayPlane);
    const auto range = resolveDisplayRange(dataset, request.field,
        request.maximumLevel, rangeMode, userRange, logarithmic,
        result.slice.plane);
    result.minimum = range.minimum;
    result.maximum = range.maximum;
    result.logarithmic = range.logarithmic;
    result.fieldName = dataset->metadata().fields[request.field.value].name;
    result.rasterUnchanged = !rasterDirty;
    if (rasterDirty) {
        result.image = renderScalarPlane(result.slice.plane,
            ScalarRenderSettings{
                .minimum = range.minimum,
                .maximum = range.maximum,
                .logarithmic = range.logarithmic,
                .palette = &palette
            });
    }
    if (isContourMode(displayMode)) {
        result.contourPlane = std::move(contourPlane);
        result.contourFinePlane = std::move(contourFinePlane);
        result.contourFineFactor = contourFineFactor;
        const auto values = contourValues(range.minimum, range.maximum, contourCount);
        result.contourPolylines = contourPolylinesForDisplay(
            result.contourFinePlane, contourFineFactor, values,
            request.outputSize[0], request.outputSize[1]);
    }
    if (displayMode == DisplayMode::VelocityVectors) {
        result.vectors = std::move(vectors);
    }
    return result;
}

// Opens one plotfile on a worker thread and renders the slice(s) described
// by spec — one per ortho view for 3-D, the single y-normal view for 2-D.
// Shared by the initial open path (default spec) and the sequence path
// (spec preserving the user's UI state across frames).
InitialSliceResult executeFrameLoad(const std::filesystem::path& path,
    DatasetId datasetId, const FrameSliceSpec& spec, std::stop_token cancellation)
{
    InitialSliceResult result;
    result.dataset = std::make_shared<PlotfileDataset>(
        path, datasetId, initialCacheBudget);
    const auto& metadata = result.dataset->metadata();
    if (metadata.fields.empty()) {
        throw std::runtime_error("dataset has no scalar fields to display");
    }
    {
        std::ifstream header(path / "Header");
        std::getline(header, result.fileVersion);
        while (!result.fileVersion.empty() && result.fileVersion.back() == '\r') {
            result.fileVersion.pop_back();
        }
    }

    const auto fieldCount = static_cast<std::uint32_t>(metadata.fields.size());
    const auto field = std::min(spec.field, fieldCount - 1);
    // An out-of-range exact level falls back to finest-available, matching
    // the level combo's behavior when a frame has fewer levels.
    const auto levelSelection = spec.levelSelection >= 0
        && spec.levelSelection <= metadata.finestLevel ? spec.levelSelection : -1;
    std::array<double, 3> positions{0.0, 0.0, 0.0};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto lower = metadata.physicalDomain.lower[axis];
        const auto upper = metadata.physicalDomain.upper[axis];
        positions[axis] = spec.defaultPositions
            ? lower + 0.5 * (upper - lower)
            : std::clamp(spec.slicePositions[axis], lower,
                std::nextafter(upper, lower));
    }

    // 3-D datasets display all three orthogonal planes at once; the slices
    // share the dataset cache, so the sequential queries are bounded. 2-D
    // keeps its single y-normal display plane.
    const std::vector<int> normals = metadata.dimension == 3
        ? std::vector<int>{0, 1, 2} : std::vector<int>{1};
    result.displays.reserve(normals.size());
    for (std::size_t entry = 0; entry < normals.size(); ++entry) {
        const auto normal = normals[entry];
        SliceRequest request;
        request.dataset = datasetId;
        request.field = FieldId{spec.displayMode == DisplayMode::VelocityVectors
            ? std::min(spec.vectorUField, fieldCount - 1) : field};
        request.normalDirection = normal;
        // A preserved zoom region is clipped to the new frame's domain; if
        // it no longer intersects at all, fall back to the whole domain.
        auto region = entry < spec.visibleRegions.size()
            && spec.visibleRegions[entry].has_value()
                ? *spec.visibleRegions[entry] : metadata.physicalDomain;
        for (int axis = 0; axis < metadata.dimension; ++axis) {
            const auto index = static_cast<std::size_t>(axis);
            auto lower = std::max(region.lower[index],
                metadata.physicalDomain.lower[index]);
            auto upper = std::min(region.upper[index],
                metadata.physicalDomain.upper[index]);
            if (!(lower < upper)) {
                lower = metadata.physicalDomain.lower[index];
                upper = metadata.physicalDomain.upper[index];
            }
            region.lower[index] = lower;
            region.upper[index] = upper;
        }
        request.visibleRegion = region;
        request.outputSize = finestNativeOutputSize(
            metadata, request.visibleRegion, request.normalDirection);
        request.composition = levelSelection < 0
            ? CompositionPolicy::FinestAvailable : CompositionPolicy::ExactLevel;
        request.maximumLevel = levelSelection < 0
            ? metadata.finestLevel : levelSelection;
        if (metadata.dimension == 3) {
            request.physicalPosition = positions[static_cast<std::size_t>(normal)];
        }
        auto display = executeSlice(result.dataset, request, spec.rangeMode,
            spec.userRange, spec.logarithmic, spec.palette, cancellation);
        display.mode = spec.displayMode;
        display.vectorVField = spec.vectorVField;
        display.contourCount = spec.contourCount;
        if (isContourMode(spec.displayMode)) {
            appendContours(result.dataset, request, spec.contourCount,
                display.minimum, display.maximum, cancellation, display);
        }
        if (spec.displayMode == DisplayMode::VelocityVectors) {
            appendVectorGlyphs(result.dataset, request,
                FieldId{std::min(spec.vectorVField, fieldCount - 1)},
                spec.contourCount, cancellation, display);
        }
        result.displays.push_back(std::move(display));
    }
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Amrvis2"));
    resize(960, 720);

    // The plot area is a stacked widget: page 0 holds the single 2-D view,
    // page 1 the 3-D grid (XY top-left, XZ top-right, YZ bottom-left, iso
    // wireframe bottom-right).
    m_stack = new QStackedWidget(this);

    m_view2d.normal = 1;
    m_view2d.label = QStringLiteral("2-D");
    m_view2d.view = new ImageView(m_stack);
    m_view2d.view->setMinimumSize(320, 240);
    m_view2d.view->setPlaceholder(tr("Open an AMReX dataset to display a slice"));
    m_stack->addWidget(m_view2d.view);

    auto* gridPage = new QWidget(m_stack);
    auto* gridLayout = new QGridLayout(gridPage);
    gridLayout->setSpacing(2);
    gridLayout->setContentsMargins(2, 2, 2, 2);
    constexpr std::array<const char*, 3> viewLabels{"YZ", "XZ", "XY"};
    for (int normal = 0; normal < 3; ++normal) {
        auto& state = m_planeViews[static_cast<std::size_t>(normal)];
        state.normal = normal;
        state.label = QString::fromLatin1(viewLabels[static_cast<std::size_t>(normal)]);
        state.view = new ImageView(gridPage);
        state.view->setMinimumSize(200, 150);
        state.view->setSliceMoveEnabled(true);
        state.view->setPlaceholder(tr("%1 view").arg(state.label));
    }
    m_isoWidget = new IsoWidget(gridPage);
    m_isoWidget->setColorPalette(&m_palette);
    gridLayout->addWidget(m_planeViews[2].view, 0, 0);  // XY: plane normal to Z
    gridLayout->addWidget(m_planeViews[1].view, 0, 1);  // XZ: plane normal to Y
    gridLayout->addWidget(m_planeViews[0].view, 1, 0);  // YZ: plane normal to X
    gridLayout->addWidget(m_isoWidget, 1, 1);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
    gridLayout->setRowStretch(0, 1);
    gridLayout->setRowStretch(1, 1);
    m_stack->addWidget(gridPage);
    m_stack->setCurrentIndex(0);
    setCentralWidget(m_stack);

    auto* sliceToolbar = addToolBar(tr("Slice Controls"));
    sliceToolbar->setMovable(false);
    sliceToolbar->addWidget(new QLabel(tr("Field:"), sliceToolbar));
    m_fieldSelector = new QComboBox(sliceToolbar);
    m_fieldSelector->setMinimumContentsLength(10);
    sliceToolbar->addWidget(m_fieldSelector);
    sliceToolbar->addSeparator();
    sliceToolbar->addWidget(new QLabel(tr("Level:"), sliceToolbar));
    m_levelSelector = new QComboBox(sliceToolbar);
    sliceToolbar->addWidget(m_levelSelector);
    sliceToolbar->addSeparator();
    // 3-D shared slice positions: one compact spinbox per axis. The whole
    // group stays hidden for 2-D datasets.
    m_slicePositionControls = new QWidget(sliceToolbar);
    auto* positionLayout = new QHBoxLayout(m_slicePositionControls);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    positionLayout->setSpacing(4);
    positionLayout->addWidget(new QLabel(tr("Position:"), m_slicePositionControls));
    constexpr std::array<const char*, 3> axisLabels{"X:", "Y:", "Z:"};
    for (int axis = 0; axis < 3; ++axis) {
        positionLayout->addWidget(new QLabel(
            QString::fromLatin1(axisLabels[static_cast<std::size_t>(axis)]),
            m_slicePositionControls));
        auto* spin = new QDoubleSpinBox(m_slicePositionControls);
        spin->setDecimals(8);
        spin->setMinimumWidth(110);
        positionLayout->addWidget(spin);
        m_sliceSpinboxes[static_cast<std::size_t>(axis)] = spin;
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this, axis](double value) {
                if (!m_controlsReady || !m_dataset
                    || m_dataset->metadata().dimension != 3) {
                    return;
                }
                setSlicePosition(axis, value);
            });
    }
    sliceToolbar->addWidget(m_slicePositionControls);
    m_slicePositionControls->setVisible(false);
    addToolBarBreak(Qt::TopToolBarArea);
    auto* rangeToolbar = addToolBar(tr("Color and Overlay Controls"));
    rangeToolbar->setMovable(false);
    rangeToolbar->addWidget(new QLabel(tr("Range:"), rangeToolbar));
    m_rangeMode = new QComboBox(rangeToolbar);
    m_rangeMode->addItem(tr("Visible"), static_cast<int>(RangeMode::Visible));
    m_rangeMode->addItem(tr("Level"), static_cast<int>(RangeMode::Level));
    m_rangeMode->addItem(tr("File"), static_cast<int>(RangeMode::File));
    m_rangeMode->addItem(tr("User"), static_cast<int>(RangeMode::User));
    rangeToolbar->addWidget(m_rangeMode);
    m_rangeMinimum = new QDoubleSpinBox(rangeToolbar);
    m_rangeMaximum = new QDoubleSpinBox(rangeToolbar);
    for (auto* range : {m_rangeMinimum, m_rangeMaximum}) {
        range->setDecimals(8);
        range->setRange(-std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max());
        range->setMinimumWidth(110);
        range->setEnabled(false);
        rangeToolbar->addWidget(range);
    }
    m_rangeMinimum->setPrefix(tr("min "));
    m_rangeMaximum->setPrefix(tr("max "));
    m_rangeMaximum->setValue(1.0);
    m_logarithmic = new QCheckBox(tr("Log"), rangeToolbar);
    rangeToolbar->addWidget(m_logarithmic);
    m_gridBoxes = new QCheckBox(tr("Grid boxes"), rangeToolbar);
    rangeToolbar->addWidget(m_gridBoxes);

    m_sliceDebounce = new QTimer(this);
    m_sliceDebounce->setSingleShot(true);
    m_sliceDebounce->setInterval(100);
    connect(m_sliceDebounce, &QTimer::timeout, this, [this] { flushSliceRequests(); });
    connect(m_fieldSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { scheduleSliceRequest(); });
    connect(m_levelSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { scheduleSliceRequest(); });
    connect(m_rangeMode, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) {
            const auto userRange = static_cast<RangeMode>(
                m_rangeMode->currentData().toInt()) == RangeMode::User;
            m_rangeMinimum->setEnabled(userRange && m_controlsReady);
            m_rangeMaximum->setEnabled(userRange && m_controlsReady);
            scheduleSliceRequest();
        });
    connect(m_rangeMinimum, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this](double) {
            if (static_cast<RangeMode>(m_rangeMode->currentData().toInt())
                == RangeMode::User) {
                scheduleSliceRequest();
            }
        });
    connect(m_rangeMaximum, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this](double) {
            if (static_cast<RangeMode>(m_rangeMode->currentData().toInt())
                == RangeMode::User) {
                scheduleSliceRequest();
            }
        });
    connect(m_logarithmic, &QCheckBox::toggled,
        this, [this](bool) { scheduleSliceRequest(); });
    connect(m_gridBoxes, &QCheckBox::toggled,
        this, [this](bool) { updateGridBoxes(); });
    m_fieldSelector->setEnabled(false);
    m_levelSelector->setEnabled(false);
    m_rangeMode->setEnabled(false);
    m_logarithmic->setEnabled(false);
    m_gridBoxes->setEnabled(false);

    m_metadataDock = new QDockWidget(tr("Dataset Metadata"), this);
    m_metadataTree = new QTreeWidget(m_metadataDock);
    m_metadataTree->setColumnCount(2);
    m_metadataTree->setHeaderLabels({tr("Property"), tr("Value")});
    m_metadataTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_metadataDock->setWidget(m_metadataTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_metadataDock);
    m_metadataDock->setVisible(false);

    m_diagnosticsDock = new QDockWidget(tr("Diagnostics"), this);
    m_diagnostics = new QPlainTextEdit(m_diagnosticsDock);
    m_diagnostics->setReadOnly(true);
    m_diagnosticsDock->setWidget(m_diagnostics);
    addDockWidget(Qt::BottomDockWidgetArea, m_diagnosticsDock);
    m_diagnosticsDock->setVisible(false);

    m_colorBarDock = new QDockWidget(tr("Color Scale"), this);
    m_colorBar = new ColorBarWidget(m_colorBarDock);
    m_colorBarDock->setWidget(m_colorBar);
    addDockWidget(Qt::RightDockWidgetArea, m_colorBarDock);

    m_animationDock = new QDockWidget(tr("Animation"), this);
    m_animationPanel = new AnimationPanel(m_animationDock);
    m_animationDock->setWidget(m_animationPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_animationDock);
    // Shown only for 3-D datasets (slice sweep) or plotfile sequences; hidden
    // until updateAnimationDockVisibility() decides otherwise.
    m_animationDock->setVisible(false);

    // One playback timer drives either animation mode; starting one mode
    // stops the other (see setPlaybackMode).
    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, [this] { playbackTick(); });
    applySpeed();
    connect(m_animationPanel, &AnimationPanel::sweepStepRequested, this,
        [this](int direction) { stepSweep(direction); });
    connect(m_animationPanel, &AnimationPanel::sweepPlayToggled, this,
        [this] { toggleSweepPlayback(); });
    connect(m_animationPanel, &AnimationPanel::sequenceStepRequested, this,
        [this](int direction) { stepSequence(direction); });
    connect(m_animationPanel, &AnimationPanel::sequencePlayToggled, this,
        [this] { toggleSequencePlayback(); });
    connect(m_animationPanel, &AnimationPanel::sequenceFrameRequested, this,
        [this](int index) { goToSequenceFrame(index); });
    connect(m_animationPanel, &AnimationPanel::speedChanged, this,
        [this](int) {
            applySpeed();
            saveSettings();
        });

    createMenus();

    connect(m_fieldSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { syncMenuChecks(); });
    connect(m_levelSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { syncMenuChecks(); });
    connect(m_gridBoxes, &QCheckBox::toggled,
        this, [this](bool) { saveSettings(); });
    connect(m_rangeMode, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { saveSettings(); });
    connect(m_logarithmic, &QCheckBox::toggled,
        this, [this](bool) { saveSettings(); });

    wireView(m_view2d);
    for (auto& state : m_planeViews) {
        wireView(state);
    }

    m_probeLabel = new QLabel(statusBar());
    statusBar()->addPermanentWidget(m_probeLabel);
    statusBar()->showMessage(tr("No dataset open"));
    updateDiagnostics();
    restoreSettings();
}

void MainWindow::wireView(PlaneViewState& state)
{
    auto* view = state.view;
    connect(view, &ImageView::probeClicked, this,
        [this, &state](int x, int displayY) { probeClicked(state, x, displayY); });
    connect(view, &ImageView::probeMoved, this,
        [this, &state](int x, int displayY) { probeMoved(state, x, displayY); });
    connect(view, &ImageView::rubberBandSelected, this,
        [this, &state](const QRectF& sceneRect) { rubberBandZoom(state, sceneRect); });
    connect(view, &ImageView::linePlotRequested, this,
        [this, &state](int x, int y, Qt::MouseButton button) {
            linePlotRequested(state, x, y, button);
        });
    connect(view, &ImageView::sliceMoveRequested, this,
        [this, &state](int x, int y, Qt::MouseButton button) {
            sliceMoveRequested(state, x, y, button);
        });
    connect(view, &ImageView::fitRequested, this,
        [this, &state] { fitView(state); });
}

std::vector<MainWindow::PlaneViewState*> MainWindow::currentViews()
{
    if (m_viewDimension == 3) {
        return {&m_planeViews[0], &m_planeViews[1], &m_planeViews[2]};
    }
    if (m_viewDimension == 2) {
        return {&m_view2d};
    }
    return {};
}

void MainWindow::setActiveView(PlaneViewState& state)
{
    if (m_activeView == &state) {
        return;
    }
    m_activeView = &state;
    if (state.plane.width <= 0 || state.plane.height <= 0) {
        return;
    }
    // The color scale and range boxes track the active view.
    m_colorBar->setLogarithmic(state.displayLogarithmic);
    m_colorBar->setFieldRange(state.displayLogarithmic
        ? state.fieldName + tr(" (log)") : state.fieldName,
        state.displayMinimum, state.displayMaximum);
    if (m_logarithmic->isChecked() != state.displayLogarithmic) {
        const QSignalBlocker logarithmicBlocker(m_logarithmic);
        m_logarithmic->setChecked(state.displayLogarithmic);
    }
    if (static_cast<RangeMode>(m_rangeMode->currentData().toInt())
        != RangeMode::User) {
        const QSignalBlocker minimumBlocker(m_rangeMinimum);
        const QSignalBlocker maximumBlocker(m_rangeMaximum);
        m_rangeMinimum->setValue(state.displayMinimum);
        m_rangeMaximum->setValue(state.displayMaximum);
    }
}

std::array<int, 2> MainWindow::displayAxes(int normal) const
{
    std::array<int, 2> axes{0, 1};
    if (m_dataset && m_dataset->metadata().dimension == 3) {
        std::size_t next = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (axis != normal) {
                axes[next++] = axis;
            }
        }
    }
    return axes;
}

void MainWindow::createMenus()
{
    auto* openAction = new QAction(tr("&Open Plotfile Directory..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] { chooseDataset(); });

    auto* openSequenceAction = new QAction(tr("Open Plotfile &Sequence..."), this);
    connect(openSequenceAction, &QAction::triggered, this,
        [this] { choosePlotfileSequence(); });

    auto* openStandaloneAction = new QAction(tr("Open &Standalone FAB/MultiFab..."), this);
    connect(openStandaloneAction, &QAction::triggered,
        this, [this] { chooseStandaloneDataset(); });

    m_paletteGroup = new QActionGroup(this);
    auto* paletteMenu = new QMenu(tr("&Palette"), this);
    for (std::size_t index = 0; index < builtinPalettes.size(); ++index) {
        const auto fileName = builtinPaletteName(builtinPalettes[index]);
        auto* action = new QAction(QString::fromLatin1(fileName.data(),
            static_cast<qsizetype>(fileName.size())), paletteMenu);
        action->setCheckable(true);
        action->setActionGroup(m_paletteGroup);
        connect(action, &QAction::triggered, this,
            [this, index] { selectBuiltinPalette(static_cast<int>(index)); });
        paletteMenu->addAction(action);
    }
    paletteMenu->addSeparator();
    auto* loadPaletteAction = paletteMenu->addAction(tr("&Load Palette File..."));
    connect(loadPaletteAction, &QAction::triggered, this, [this] { loadPaletteFile(); });

    auto* exportAction = new QAction(tr("&Export Image..."), this);
    connect(exportAction, &QAction::triggered, this, [this] { exportImage(); });

    auto* exportDataAction = new QAction(tr("Export Slice &Data (ASCII)..."), this);
    connect(exportDataAction, &QAction::triggered,
        this, [this] { exportSliceData(); });

    auto* quitAction = new QAction(tr("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(openSequenceAction);
    fileMenu->addAction(openStandaloneAction);
    fileMenu->addMenu(paletteMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction);
    fileMenu->addAction(exportDataAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    auto* scaleGroup = new QActionGroup(this);
    auto* scaleMenu = new QMenu(tr("&Scale"), this);
    m_fitScaleAction = new QAction(tr("&Fit to Window"), scaleMenu);
    m_fitScaleAction->setCheckable(true);
    m_fitScaleAction->setActionGroup(scaleGroup);
    m_fitScaleAction->setChecked(true);
    m_fitScaleAction->setShortcut(QKeySequence(Qt::Key_0));
    connect(m_fitScaleAction, &QAction::triggered,
        this, [this] { fitViewToWindow(); });
    scaleMenu->addAction(m_fitScaleAction);
    constexpr std::array<int, 6> fixedScales{1, 2, 4, 8, 16, 32};
    for (std::size_t index = 0; index < fixedScales.size(); ++index) {
        const auto factor = fixedScales[index];
        auto* action = new QAction(tr("%1x").arg(factor), scaleMenu);
        action->setCheckable(true);
        action->setActionGroup(scaleGroup);
        action->setShortcut(QKeySequence(Qt::Key_1 + static_cast<int>(index)));
        connect(action, &QAction::triggered, this, [this, factor] {
            if (m_activeView != nullptr) {
                m_activeView->view->setFixedScale(factor);
            }
        });
        scaleMenu->addAction(action);
    }

    m_levelMenu = new QMenu(tr("&Level"), this);
    m_levelGroup = new QActionGroup(this);
    m_levelMenu->setEnabled(false);

    m_boxesAction = new QAction(tr("&Boxes"), this);
    m_boxesAction->setCheckable(true);
    m_boxesAction->setShortcut(QKeySequence(Qt::Key_B));
    connect(m_boxesAction, &QAction::toggled, this, [this](bool checked) {
        m_gridBoxes->setChecked(checked);
    });
    connect(m_gridBoxes, &QCheckBox::toggled, this, [this](bool checked) {
        m_boxesAction->setChecked(checked);
    });

    m_contoursAction = new QAction(tr("&Contours..."), this);
    m_contoursAction->setEnabled(false);
    connect(m_contoursAction, &QAction::triggered,
        this, [this] { showContoursDialog(); });

    m_datasetAction = new QAction(tr("&Dataset..."), this);
    m_datasetAction->setEnabled(false);
    m_datasetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(m_datasetAction, &QAction::triggered,
        this, [this] { showDatasetWindow(); });

    // Legacy View menu order: Contours..., Range..., Dataset..., Number
    // Format... (the range lives in the toolbar here, not in a dialog).
    auto* numberFormatAction = new QAction(tr("&Number Format..."), this);
    connect(numberFormatAction, &QAction::triggered,
        this, [this] { showNumberFormatDialog(); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addMenu(scaleMenu);
    viewMenu->addMenu(m_levelMenu);
    viewMenu->addAction(m_boxesAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_contoursAction);
    viewMenu->addAction(m_datasetAction);
    viewMenu->addAction(numberFormatAction);
    viewMenu->addSeparator();
    // Panel visibility toggles. Color Scale is visible by default; Dataset
    // Metadata and Diagnostics start hidden, and Animation is auto-shown for
    // 3-D datasets and plotfile sequences.
    viewMenu->addAction(m_metadataDock->toggleViewAction());
    viewMenu->addAction(m_colorBarDock->toggleViewAction());
    viewMenu->addAction(m_diagnosticsDock->toggleViewAction());
    viewMenu->addAction(m_animationDock->toggleViewAction());

    m_variableMenu = menuBar()->addMenu(tr("&Variable"));
    m_variableGroup = new QActionGroup(this);
    m_variableMenu->setEnabled(false);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* referenceAction = new QAction(tr("&Keyboard && Mouse..."), this);
    connect(referenceAction, &QAction::triggered,
        this, [this] { showKeyboardMouseReference(); });
    auto* aboutAction = new QAction(tr("&About Amrvis2..."), this);
    connect(aboutAction, &QAction::triggered, this, [this] { showAboutDialog(); });
    helpMenu->addAction(referenceAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);
}

void MainWindow::rebuildVariableMenu()
{
    m_variableMenu->clear();
    if (!m_dataset) {
        return;
    }
    const auto& fields = m_dataset->metadata().fields;
    for (std::size_t field = 0; field < fields.size(); ++field) {
        auto* action = new QAction(QString::fromStdString(fields[field].name),
            m_variableMenu);
        action->setCheckable(true);
        action->setActionGroup(m_variableGroup);
        const auto index = static_cast<int>(field);
        connect(action, &QAction::triggered, this,
            [this, index] { m_fieldSelector->setCurrentIndex(index); });
        m_variableMenu->addAction(action);
    }
    syncMenuChecks();
}

void MainWindow::rebuildLevelMenu()
{
    m_levelMenu->clear();
    if (!m_dataset) {
        return;
    }
    const auto& metadata = m_dataset->metadata();
    auto* finest = new QAction(tr("Finest available"), m_levelMenu);
    finest->setCheckable(true);
    finest->setActionGroup(m_levelGroup);
    finest->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(finest, &QAction::triggered, this,
        [this] { m_levelSelector->setCurrentIndex(0); });
    m_levelMenu->addAction(finest);
    for (int level = 0; level <= metadata.finestLevel; ++level) {
        auto* action = new QAction(tr("Level %1 only").arg(level), m_levelMenu);
        action->setCheckable(true);
        action->setActionGroup(m_levelGroup);
        if (level < 9) {
            action->setShortcut(QKeySequence(
                Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + level)));
        }
        connect(action, &QAction::triggered, this,
            [this, level] { m_levelSelector->setCurrentIndex(level + 1); });
        m_levelMenu->addAction(action);
    }
    syncMenuChecks();
}

void MainWindow::syncMenuChecks()
{
    const auto fieldIndex = m_fieldSelector->currentIndex();
    const auto fieldActions = m_variableMenu->actions();
    for (int index = 0; index < fieldActions.size(); ++index) {
        fieldActions[index]->setChecked(index == fieldIndex);
    }
    const auto levelIndex = m_levelSelector->currentIndex();
    const auto levelActions = m_levelMenu->actions();
    for (int index = 0; index < levelActions.size(); ++index) {
        levelActions[index]->setChecked(index == levelIndex);
    }
}

void MainWindow::syncPaletteChecks()
{
    const auto actions = m_paletteGroup->actions();
    for (int index = 0; index < actions.size(); ++index) {
        actions[index]->setChecked(!m_paletteFromFile && index == m_builtinIndex);
    }
}

void MainWindow::selectBuiltinPalette(int index)
{
    if (index < 0 || index >= static_cast<int>(builtinPalettes.size())) {
        return;
    }
    applyPalette(builtinPalette(builtinPalettes[static_cast<std::size_t>(index)]),
        index, QString());
}

void MainWindow::loadPaletteFile()
{
    const auto settings = makeSettings();
    const auto filename = QFileDialog::getOpenFileName(this,
        tr("Load Palette File"),
        settings.value(QStringLiteral("lastOpenDirectory")).toString(),
        tr("Legacy palette files (*.pal);;All files (*)"));
    if (filename.isEmpty()) {
        return;
    }
    try {
        applyPalette(Palette::load(filename.toStdString()), std::nullopt, filename);
        auto writableSettings = makeSettings();
        writableSettings.setValue(QStringLiteral("lastOpenDirectory"),
            QFileInfo(filename).absolutePath());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Cannot load palette"),
            QString::fromUtf8(error.what()));
    }
}

void MainWindow::applyPalette(const Palette& palette, std::optional<int> builtinIndex,
    const QString& filePath)
{
    m_palette = palette;
    m_paletteFromFile = !builtinIndex.has_value();
    if (builtinIndex.has_value()) {
        m_builtinIndex = *builtinIndex;
        m_paletteFilePath.clear();
    } else {
        m_paletteFilePath = filePath;
    }
    m_colorBar->setPalette(&m_palette);
    syncPaletteChecks();
    saveSettings();
    scheduleSliceRequest();
    updateGridBoxes();
    updateOverlays();
    updateCrosshairs();
    m_isoWidget->update();
}

void MainWindow::showContoursDialog()
{
    if (!m_dataset) {
        return;
    }
    const auto& fields = m_dataset->metadata().fields;
    std::vector<std::string> fieldNames;
    fieldNames.reserve(fields.size());
    for (const auto& field : fields) {
        fieldNames.push_back(field.name);
    }
    SetContoursDialog dialog(fieldNames, this);
    dialog.setMode(m_displayMode);
    dialog.setContourCount(m_contourCount);
    dialog.setVectorFields(m_vectorUField, m_vectorVField);
    connect(&dialog, &SetContoursDialog::applied, this, [this, &dialog] {
        applyContourSettings(dialog.mode(), dialog.contourCount(),
            dialog.uField(), dialog.vField());
    });
    dialog.exec();
}

void MainWindow::applyContourSettings(
    DisplayMode mode, int count, int uField, int vField)
{
    if (mode == DisplayMode::VelocityVectors && m_dataset
        && m_dataset->metadata().fields.size() < 2) {
        statusBar()->showMessage(
            tr("Velocity Vectors requires at least two fields"));
        mode = DisplayMode::Raster;
    }
    const auto previousMode = m_displayMode;
    const auto previousCount = m_contourCount;
    const auto previousUField = m_vectorUField;
    const auto previousVField = m_vectorVField;
    m_displayMode = mode;
    m_contourCount = count;
    m_vectorUField = uField;
    m_vectorVField = vField;
    saveSettings();

    // Contour polylines are re-extracted from the cached data-resolution
    // planes on the worker, and the raster never depends on the contour
    // mode or count, so these changes request with rasterDirty = false;
    // requestSlice satisfies them from the cache whenever the slice spec is
    // unchanged (entering a contour or vector mode without a matching cache
    // still takes the full path). Vector glyphs are baked into the slice
    // result, so a vector-mode count change re-slices. Drop stale glyphs
    // while a vector-mode request is in flight.
    const auto involvesVectors = mode == DisplayMode::VelocityVectors
        || previousMode == DisplayMode::VelocityVectors;
    const auto inputsChanged = mode != previousMode || count != previousCount
        || uField != previousUField || vField != previousVField;
    if (inputsChanged) {
        if (involvesVectors) {
            for (auto* state : currentViews()) {
                state->vectorSegments.clear();
                state->view->setOverlaySegments({});
            }
        }
        scheduleSliceRequest(false);
    } else {
        updateOverlays();
    }
}

void MainWindow::showNumberFormatDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Number Format"));
    dialog.setModal(true);

    auto* edit = new QLineEdit(m_numberFormat, &dialog);
    edit->setMinimumWidth(160);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
        | QDialogButtonBox::Cancel, &dialog);
    auto* defaultButton = buttons->addButton(
        tr("Default"), QDialogButtonBox::ResetRole);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(edit);
    layout->addWidget(buttons);

    connect(defaultButton, &QPushButton::clicked, edit, [edit] {
        edit->setText(defaultNumberFormat());
    });
    // OK validates first; an invalid format warns and keeps the dialog open.
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
        [this, &dialog, edit] {
            const auto format = edit->text();
            if (!isValidNumberFormat(format)) {
                QMessageBox::warning(&dialog, tr("Invalid number format"),
                    tr("\"%1\" is not a usable number format.\n"
                       "Use a printf-style format with exactly one floating "
                       "conversion, e.g. %2.")
                        .arg(format, defaultNumberFormat()));
                return;
            }
            applyNumberFormat(format);
            dialog.accept();
        });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::applyNumberFormat(const QString& format)
{
    if (!isValidNumberFormat(format) || m_numberFormat == format) {
        return;
    }
    m_numberFormat = format;
    m_colorBar->setNumberFormat(format);
    // Open child windows repaint against the stored format; a null pointer
    // means the window picks the format up when it is next created.
    if (m_datasetWindow != nullptr) {
        m_datasetWindow->setNumberFormat(format);
    }
    if (m_linePlotWindow != nullptr) {
        m_linePlotWindow->setNumberFormat(format);
    }
    saveSettings();
}

void MainWindow::validateVectorMode()
{
    if (m_displayMode != DisplayMode::VelocityVectors) {
        return;
    }
    const auto fieldCount = m_openMetadata ? m_openMetadata->fields.size() : 0;
    if (fieldCount < 2) {
        statusBar()->showMessage(
            tr("Velocity Vectors requires at least two fields"));
        m_displayMode = DisplayMode::Raster;
        return;
    }
    ensureVectorFieldDefaults();
}

void MainWindow::ensureVectorFieldDefaults()
{
    if (!m_openMetadata) {
        return;
    }
    const auto& fields = m_openMetadata->fields;
    const auto count = static_cast<int>(fields.size());
    if (m_vectorUField >= 0 && m_vectorUField < count
        && m_vectorVField >= 0 && m_vectorVField < count) {
        return;
    }
    std::vector<std::string> fieldNames;
    fieldNames.reserve(fields.size());
    for (const auto& field : fields) {
        fieldNames.push_back(field.name);
    }
    const auto [uField, vField] = detectVectorFields(fieldNames);
    m_vectorUField = uField;
    m_vectorVField = vField;
}

QLineF MainWindow::planeSegmentToScene(const PlaneViewState& state,
    float x0, float y0, float x1, float y1) const
{
    // Plane row 0 is the bottom row; the displayed image is mirrored
    // vertically, so scene y runs opposite to plane y (see showSlice).
    const auto top = static_cast<double>(state.plane.height) - 1.0;
    return QLineF(QPointF(x0, top - y0), QPointF(x1, top - y1));
}

QColor MainWindow::contourValueColor(const PlaneViewState& state, double value) const
{
    // Same normalization renderScalarPlane applies to every pixel.
    auto normalized = 0.0;
    if (state.displayLogarithmic) {
        const auto rangeMinimum = std::log(state.displayMinimum);
        const auto rangeMaximum = std::log(state.displayMaximum);
        normalized = (std::log(value) - rangeMinimum)
            / (rangeMaximum - rangeMinimum);
    } else {
        normalized = (value - state.displayMinimum)
            / (state.displayMaximum - state.displayMinimum);
    }
    return QColor::fromRgba(static_cast<QRgb>(m_palette.argb(normalized)));
}

QColor MainWindow::monochromeContourColor() const
{
    const auto lowest = QColor::fromRgba(static_cast<QRgb>(m_palette.argb(0.0)));
    const auto luminance = 0.2126 * lowest.redF()
        + 0.7152 * lowest.greenF() + 0.0722 * lowest.blueF();
    return luminance < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
}

QColor MainWindow::sliceAxisColor(int axis) const
{
    // Legacy Amrvis draws each slice plane's guides in a fixed palette slot:
    // x -> slot 65, y -> slot 220, z -> slot 255.
    constexpr std::array<int, 3> paletteSlots{65, 220, 255};
    return QColor::fromRgba(static_cast<QRgb>(
        m_palette.slotArgb(paletteSlots[static_cast<std::size_t>(axis)])));
}

void MainWindow::updateOverlay(PlaneViewState& state)
{
    std::vector<OverlaySegment> overlays;
    std::vector<OverlayPath> paths;
    const auto planeReady = state.plane.width > 1 && state.plane.height > 1;
    if (!planeReady || m_displayMode == DisplayMode::Raster) {
        state.view->setOverlaySegments(overlays);
        state.view->setOverlayPaths(paths);
        return;
    }

    if (m_displayMode == DisplayMode::VelocityVectors) {
        overlays.reserve(state.vectorSegments.size());
        for (const auto& segment : state.vectorSegments) {
            overlays.push_back({planeSegmentToScene(state,
                segment.x0, segment.y0, segment.x1, segment.y1),
                QColor(Qt::white), 1.0F});
        }
        state.view->setOverlaySegments(overlays);
        state.view->setOverlayPaths(paths);
        return;
    }

    if (!(state.displayMinimum < state.displayMaximum)) {
        state.view->setOverlaySegments(overlays);
        state.view->setOverlayPaths(paths);
        return;
    }
    try {
        // The polylines were extracted from the refined data-resolution
        // contour plane on the slice worker and are already in display-plane
        // pixel space (see appendContours); this thread only converts them
        // to painter paths. Plane row 0 is the bottom row; the displayed
        // image is mirrored vertically, so scene y runs opposite to plane y
        // (see showSlice).
        const auto monochrome = monochromeContourColor();
        const auto top = static_cast<double>(state.plane.height) - 1.0;
        std::map<double, QPainterPath> pathsByValue;
        for (const auto& polyline : state.contourPolylines) {
            if (polyline.points.empty()) {
                continue;
            }
            auto& path = pathsByValue[polyline.value];
            const auto& first = polyline.points.front();
            path.moveTo(QPointF(first[0], top - first[1]));
            for (std::size_t i = 1; i < polyline.points.size(); ++i) {
                const auto& point = polyline.points[i];
                path.lineTo(QPointF(point[0], top - point[1]));
            }
            if (polyline.closed) {
                path.closeSubpath();
            }
        }
        paths.reserve(pathsByValue.size());
        for (auto& [value, path] : pathsByValue) {
            const auto color = m_displayMode == DisplayMode::ColorContours
                ? contourValueColor(state, value) : monochrome;
            paths.push_back({std::move(path), color, 1.0F});
        }
    } catch (const std::exception&) {
        paths.clear();
    }
    state.view->setOverlaySegments(overlays);
    state.view->setOverlayPaths(paths);
}

void MainWindow::updateOverlays()
{
    for (auto* state : currentViews()) {
        updateOverlay(*state);
    }
}

void MainWindow::showKeyboardMouseReference()
{
    QString rows;
    const auto add = [&rows](const QString& action, const QString& description) {
        rows += QStringLiteral(
            "<tr><td style='padding-right:14px;vertical-align:top;'><b>%1</b></td>"
            "<td>%2</td></tr>").arg(action, description);
    };
    add(tr("Left click"), tr("Probe the value under the cursor"));
    add(tr("Left drag"), tr("Zoom to the rubber-band subregion"));
    add(tr("Middle drag (2-D)"), tr("Horizontal line plot"));
    add(tr("Middle drag (3-D)"),
        tr("Move the slice along the vertical axis "
           "(hold Shift or Ctrl for a line plot)"));
    add(tr("Right drag (2-D)"), tr("Vertical line plot"));
    add(tr("Right drag (3-D)"),
        tr("Move the slice along the horizontal axis "
           "(hold Shift or Ctrl for a line plot)"));
    add(tr("Wheel / double click"), tr("Zoom in or out / refit to the window"));
    add(tr("B"), tr("Toggle AMR grid boxes"));
    add(tr("0"), tr("Fit to the window"));
    add(tr("1-6"), tr("Fixed zoom scales (1x-32x)"));
    add(tr("Ctrl+0"), tr("Composite the finest available level"));
    add(tr("Ctrl+1-9"), tr("Show one exact AMR level"));
    add(tr("Ctrl+D"), tr("Open the Dataset window (raw cell values per level)"));

    QMessageBox box(this);
    box.setWindowTitle(tr("Keyboard & Mouse"));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<table>%1</table>").arg(rows));
    box.setInformativeText(
        tr("View \xE2\x86\x92 Number Format... sets the readout format; "
           "the View menu shows or hides the panels."));
    box.setIcon(QMessageBox::NoIcon);
    box.exec();
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, tr("About Amrvis2"),
        tr("<h3>Amrvis2</h3>"
           "<p>Demand-driven AMR visualization.</p>"
           "<p>Version %1</p>"
           "<p>A C++20 / Qt 6 application for inspecting AMReX plotfiles.</p>")
            .arg(QStringLiteral(AMRVIS_VERSION)));
}

void MainWindow::fitView(PlaneViewState& state)
{
    state.visibleRegion.reset();
    state.view->fitToWindow();
    m_fitScaleAction->setChecked(true);
    scheduleSliceRequest(state);
}

void MainWindow::fitViewToWindow()
{
    for (auto* state : currentViews()) {
        fitView(*state);
    }
}

QString MainWindow::probeReadout(
    const PlaneViewState& state, int x, int displayY) const
{
    const auto& plane = state.plane;
    if (!m_dataset || plane.width <= 0 || plane.height <= 0) {
        return tr("no data");
    }
    const auto y = plane.height - 1 - displayY;
    const auto offset = static_cast<std::size_t>(x)
        + static_cast<std::size_t>(plane.width) * static_cast<std::size_t>(y);
    if (offset >= plane.values.size() || plane.valid[offset] == 0) {
        return tr("no data");
    }
    const auto& metadata = m_dataset->metadata();
    const auto axes = displayAxes(state.normal);
    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    std::array<double, 3> position{0.0, 0.0, 0.0};
    position[xAxis] = plane.physicalRegion.lower[xAxis]
        + (static_cast<double>(x) + 0.5)
            / static_cast<double>(plane.width)
            * (plane.physicalRegion.upper[xAxis]
                - plane.physicalRegion.lower[xAxis]);
    position[yAxis] = plane.physicalRegion.lower[yAxis]
        + (static_cast<double>(y) + 0.5)
            / static_cast<double>(plane.height)
            * (plane.physicalRegion.upper[yAxis]
                - plane.physicalRegion.lower[yAxis]);
    if (metadata.dimension == 3) {
        position[static_cast<std::size_t>(state.normal)]
            = m_slicePosition3d[static_cast<std::size_t>(state.normal)];
    }
    const auto level = std::clamp(
        static_cast<int>(plane.sourceLevel[offset]), 0, metadata.finestLevel);
    const auto& levelMetadata = metadata.levels[static_cast<std::size_t>(level)];

    // Integer index of the cell/face/edge/node. Nodes sit on integer
    // positions so they round; everything else floors into its cell.
    const auto centering = (state.hasCachedRequest
            && state.cachedRequest.field.value < metadata.fields.size())
        ? metadata.fields[state.cachedRequest.field.value].centering
        : amrvis::Centering::Cell;
    const auto isNode = centering == amrvis::Centering::Node;
    std::array<int, 3> cell{0, 0, 0};
    for (int axis = 0; axis < metadata.dimension; ++axis) {
        const auto i = static_cast<std::size_t>(axis);
        const auto normalized = (position[i] - metadata.physicalDomain.lower[i])
            / levelMetadata.cellSize[i];
        cell[i] = isNode ? static_cast<int>(std::lround(normalized))
                         : static_cast<int>(std::floor(normalized));
    }

    // The AMR box (grid) at this level that contains the cell.
    int boxIndex = -1;
    for (int box = 0; box < static_cast<int>(levelMetadata.boxes.size()); ++box) {
        const auto& candidate = levelMetadata.boxes[static_cast<std::size_t>(box)];
        bool contains = true;
        for (int axis = 0; axis < metadata.dimension; ++axis) {
            const auto i = static_cast<std::size_t>(axis);
            if (cell[i] < candidate.lower[i] || cell[i] > candidate.upper[i]) {
                contains = false;
                break;
            }
        }
        if (contains) {
            boxIndex = box;
            break;
        }
    }

    auto join = [&](const auto& triple) {
        QString text;
        for (int axis = 0; axis < metadata.dimension; ++axis) {
            if (axis != 0) {
                text += ',';
            }
            text += QString::number(triple[static_cast<std::size_t>(axis)]);
        }
        return text;
    };

    constexpr std::array<const char*, 3> axisNames{"x", "y", "z"};
    const char* indexKind = "cell";
    if (centering == amrvis::Centering::Node) {
        indexKind = "node";
    } else if (centering == amrvis::Centering::FaceX
        || centering == amrvis::Centering::FaceY
        || centering == amrvis::Centering::FaceZ) {
        indexKind = "face";
    } else if (centering == amrvis::Centering::EdgeX
        || centering == amrvis::Centering::EdgeY
        || centering == amrvis::Centering::EdgeZ) {
        indexKind = "edge";
    }

    QString boxText;
    if (boxIndex >= 0) {
        const auto& box = levelMetadata.boxes[static_cast<std::size_t>(boxIndex)];
        boxText = tr("box #%1 (%2)-(%3)")
            .arg(boxIndex)
            .arg(join(box.lower), join(box.upper));
    } else {
        boxText = tr("box=none");
    }

    return tr("%1=%2 %3=%4 value=%5 level=%6 %7=(%8) %9")
        .arg(QString::fromLatin1(axisNames[xAxis]))
        .arg(formatNumber(position[xAxis], m_numberFormat))
        .arg(QString::fromLatin1(axisNames[yAxis]))
        .arg(formatNumber(position[yAxis], m_numberFormat))
        .arg(formatNumber(static_cast<double>(plane.values[offset]),
            m_numberFormat))
        .arg(level)
        .arg(QString::fromLatin1(indexKind))
        .arg(join(cell))
        .arg(boxText);
}

void MainWindow::probeMoved(PlaneViewState& state, int x, int displayY)
{
    setActiveView(state);
    m_probeLabel->setText(probeReadout(state, x, displayY));
}

void MainWindow::probeClicked(PlaneViewState& state, int x, int displayY)
{
    setActiveView(state);
    const auto line = probeReadout(state, x, displayY);
    m_probeLabel->setText(line);
    constexpr int maximumProbeLines = 100;
    m_probeLines.append(line);
    while (m_probeLines.size() > maximumProbeLines) {
        m_probeLines.removeFirst();
    }
    updateDiagnostics();
}

void MainWindow::rubberBandZoom(PlaneViewState& state, const QRectF& sceneRect)
{
    setActiveView(state);
    const auto& plane = state.plane;
    if (!m_dataset || plane.width <= 0 || plane.height <= 0) {
        return;
    }
    const auto clamped = sceneRect.normalized().intersected(
        QRectF(0.0, 0.0, static_cast<double>(plane.width),
            static_cast<double>(plane.height)));
    if (clamped.width() < 1.0 || clamped.height() < 1.0) {
        return;
    }
    const auto axes = displayAxes(state.normal);
    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto& region = plane.physicalRegion;
    const auto width = static_cast<double>(plane.width);
    const auto height = static_cast<double>(plane.height);
    const auto xExtent = region.upper[xAxis] - region.lower[xAxis];
    const auto yExtent = region.upper[yAxis] - region.lower[yAxis];
    auto visible = region;
    visible.lower[xAxis] = region.lower[xAxis] + clamped.left() / width * xExtent;
    visible.upper[xAxis] = region.lower[xAxis] + clamped.right() / width * xExtent;
    visible.lower[yAxis] = region.lower[yAxis]
        + (height - clamped.bottom()) / height * yExtent;
    visible.upper[yAxis] = region.lower[yAxis]
        + (height - clamped.top()) / height * yExtent;
    state.visibleRegion = visible;
    state.view->zoomToRect(clamped);
    scheduleSliceRequest(state);
}

void MainWindow::linePlotRequested(PlaneViewState& state, int imageX, int imageY,
    Qt::MouseButton button)
{
    setActiveView(state);
    const auto& plane = state.plane;
    if (!m_controlsReady || !m_dataset || plane.width <= 0 || plane.height <= 0) {
        return;
    }
    const auto dataset = m_dataset;
    const auto& metadata = dataset->metadata();
    const auto horizontal = button == Qt::MiddleButton;
    const auto level = m_levelSelector->currentData().toInt();
    const auto maximumLevel = level < 0 ? metadata.finestLevel : level;
    const auto composition = level < 0
        ? CompositionPolicy::FinestAvailable : CompositionPolicy::ExactLevel;
    const auto field = m_displayMode == DisplayMode::VelocityVectors
        ? static_cast<std::uint32_t>(std::max(m_vectorUField, 0))
        : m_fieldSelector->currentData().toUInt();
    const auto slicePosition = metadata.dimension == 3
        ? m_slicePosition3d[static_cast<std::size_t>(state.normal)] : 0.0;
    const auto request = makeLineRequest(plane.physicalRegion,
        plane.width, plane.height, imageX, imageY, horizontal,
        metadata.dimension, state.normal, slicePosition,
        dataset->id(), FieldId{field}, maximumLevel, composition);
    const auto fieldName = metadata.fields[field].name;
    const auto dimension = metadata.dimension;
    // The other in-plane axis carries the cursor's fixed coordinate.
    const auto axes = displayAxes(state.normal);
    const auto primaryFixedAxis = request.axis == axes[0] ? axes[1] : axes[0];
    const auto generation = m_generation;
    // Renew the line-plot stop source only if a dataset switch or window close
    // stopped it, so concurrent line requests can still complete and stack
    // their curves in the shared window.
    if (m_linePlotStopSource.stop_requested()) {
        m_linePlotStopSource = std::stop_source{};
    }
    const auto cancellation = m_linePlotStopSource.get_token();
    ++m_activeRequests;
    statusBar()->showMessage(tr("Loading line plot for %1...").arg(
        QString::fromStdString(fieldName)));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<LineQueryResult>(this);
    connect(watcher, &QFutureWatcher<LineQueryResult>::finished, this,
        [this, watcher, dataset, generation, cancellation, request, fieldName,
            dimension, primaryFixedAxis, maximumLevel, composition] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation != m_generation || cancellation.stop_requested()) {
                    ++m_staleResults;
                } else {
                    appendLinePlotCurve(result.line, fieldName, dimension,
                        primaryFixedAxis, request.fixedCoordinates,
                        maximumLevel, composition);
                    const auto cache = dataset->cacheMetrics();
                    m_cacheBudgetBytes = cache.budgetBytes;
                    m_cacheResidentBytes = cache.residentBytes;
                    m_cachePinnedBytes = cache.pinnedBytes;
                    m_cacheEvictions = cache.evictions;
                    m_lastBlocksRead = result.metrics.blocksRead;
                    m_lastCacheHits = result.metrics.cacheHits;
                    m_lastPayloadBytesRead = result.metrics.payloadBytesRead;
                    statusBar()->showMessage(tr("Added line plot curve for %1")
                        .arg(QString::fromStdString(fieldName)));
                }
            } catch (const std::exception& error) {
                if (generation == m_generation && !cancellation.stop_requested()) {
                    statusBar()->showMessage(tr("Line plot request failed"));
                    QMessageBox::critical(this, tr("Cannot load line plot"),
                        exceptionMessage(error));
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [dataset, request, cancellation] {
            return LineQuery(*dataset).execute(request, cancellation);
        }));
}

void MainWindow::sliceMoveRequested(PlaneViewState& state, int imageX, int imageY,
    Qt::MouseButton button)
{
    setActiveView(state);
    if (!m_dataset || m_dataset->metadata().dimension != 3
        || state.plane.width <= 0 || state.plane.height <= 0) {
        return;
    }
    // A middle (horizontal guide) drag moves the axis pointing vertically in
    // this view; a right (vertical guide) drag moves the horizontal one.
    const auto axes = displayAxes(state.normal);
    const auto& region = state.plane.physicalRegion;
    int axis = axes[0];
    auto fraction = 0.0;
    if (button == Qt::MiddleButton) {
        axis = axes[1];
        const auto planeY = state.plane.height - 1 - imageY;
        fraction = (static_cast<double>(planeY) + 0.5)
            / static_cast<double>(state.plane.height);
    } else {
        fraction = (static_cast<double>(imageX) + 0.5)
            / static_cast<double>(state.plane.width);
    }
    const auto index = static_cast<std::size_t>(axis);
    setSlicePosition(axis, region.lower[index]
        + fraction * (region.upper[index] - region.lower[index]));
}

void MainWindow::appendLinePlotCurve(const LineResult& line,
    const std::string& fieldName, int dimension, int primaryFixedAxis,
    const std::array<double, 3>& fixedCoordinates, int maximumLevel,
    CompositionPolicy composition)
{
    if (m_linePlotWindow == nullptr) {
        auto name = QString::fromStdString(m_datasetPath.filename().string());
        if (name.isEmpty()) {
            name = QString::fromStdString(m_datasetPath.string());
        }
        auto* window = new LinePlotWindow(name);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setNumberFormat(m_numberFormat);
        connect(window, &QObject::destroyed, this, [this, window] {
            if (m_linePlotWindow == window) {
                m_linePlotWindow = nullptr;
            }
            // Stop in-flight line queries so a late result cannot reopen the
            // window the user just closed.
            m_linePlotStopSource.request_stop();
        });
        m_linePlotWindow = window;
    }
    LinePlotCurve curve;
    curve.line = line;
    curve.fieldName = fieldName;
    curve.primaryFixedAxis = primaryFixedAxis;
    curve.fixedCoordinates = fixedCoordinates;
    curve.dimension = dimension;
    curve.maximumLevel = maximumLevel;
    curve.composition = composition;
    m_linePlotWindow->addCurve(std::move(curve));
    m_linePlotWindow->show();
    m_linePlotWindow->raise();
    m_linePlotWindow->activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    auto settings = makeSettings();
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreSettings()
{
    const auto settings = makeSettings();

    auto paletteRestored = false;
    if (settings.value(QStringLiteral("palette/fromFile"), false).toBool()) {
        const auto path = settings.value(QStringLiteral("palette/filePath")).toString();
        if (!path.isEmpty()) {
            try {
                m_palette = Palette::load(path.toStdString());
                m_paletteFromFile = true;
                m_paletteFilePath = path;
                paletteRestored = true;
            } catch (const std::exception&) {
                paletteRestored = false;
            }
        }
    }
    if (!paletteRestored) {
        const auto name = settings.value(QStringLiteral("palette/builtin"),
            QStringLiteral("Rainbow")).toString();
        m_builtinIndex = 0;
        for (std::size_t index = 0; index < builtinPaletteNames.size(); ++index) {
            if (name == QLatin1String(builtinPaletteNames[index])) {
                m_builtinIndex = static_cast<int>(index);
                break;
            }
        }
        m_palette = builtinPalette(
            builtinPalettes[static_cast<std::size_t>(m_builtinIndex)]);
        m_paletteFromFile = false;
        m_paletteFilePath.clear();
    }
    m_colorBar->setPalette(&m_palette);
    syncPaletteChecks();

    {
        const QSignalBlocker boxesBlocker(m_gridBoxes);
        const QSignalBlocker actionBlocker(m_boxesAction);
        const auto boxes = settings.value(QStringLiteral("view/boxes"), false).toBool();
        m_gridBoxes->setChecked(boxes);
        m_boxesAction->setChecked(boxes);
    }
    {
        const QSignalBlocker rangeBlocker(m_rangeMode);
        const auto index = settings.value(QStringLiteral("range/modeIndex"), 0).toInt();
        if (index >= 0 && index < m_rangeMode->count()) {
            m_rangeMode->setCurrentIndex(index);
        }
    }
    {
        const QSignalBlocker logarithmicBlocker(m_logarithmic);
        m_logarithmic->setChecked(
            settings.value(QStringLiteral("range/logarithmic"), false).toBool());
    }
    {
        const auto mode = settings.value(QStringLiteral("contours/mode"), 0).toInt();
        if (mode >= 0 && mode <= static_cast<int>(DisplayMode::VelocityVectors)) {
            m_displayMode = static_cast<DisplayMode>(mode);
        }
        m_contourCount = std::clamp(
            settings.value(QStringLiteral("contours/count"), 10).toInt(), 1, 99);
    }
    {
        // A stored format that no longer validates falls back to the default.
        const auto format = settings.value(QStringLiteral("numberFormat"),
            defaultNumberFormat()).toString();
        m_numberFormat = isValidNumberFormat(format) ? format
            : defaultNumberFormat();
        m_colorBar->setNumberFormat(m_numberFormat);
    }
    m_animationPanel->setSpeedValue(
        settings.value(QStringLiteral("animation/speed"), 300).toInt());
    applySpeed();
    const auto geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
}

void MainWindow::saveSettings()
{
    auto settings = makeSettings();
    settings.setValue(QStringLiteral("view/boxes"), m_gridBoxes->isChecked());
    settings.setValue(QStringLiteral("range/modeIndex"), m_rangeMode->currentIndex());
    settings.setValue(QStringLiteral("range/logarithmic"), m_logarithmic->isChecked());
    settings.setValue(QStringLiteral("palette/fromFile"), m_paletteFromFile);
    settings.setValue(QStringLiteral("palette/filePath"), m_paletteFilePath);
    settings.setValue(QStringLiteral("palette/builtin"),
        QLatin1String(builtinPaletteNames[static_cast<std::size_t>(m_builtinIndex)]));
    settings.setValue(QStringLiteral("contours/mode"),
        static_cast<int>(m_displayMode));
    settings.setValue(QStringLiteral("contours/count"), m_contourCount);
    settings.setValue(QStringLiteral("numberFormat"), m_numberFormat);
    settings.setValue(QStringLiteral("animation/speed"),
        m_animationPanel->speedValue());
}

void MainWindow::updateWindowTitle()
{
    if (!m_openMetadata) {
        setWindowTitle(tr("Amrvis2"));
        return;
    }
    const auto& metadata = *m_openMetadata;
    auto name = QString::fromStdString(m_datasetPath.filename().string());
    if (name.isEmpty()) {
        name = QString::fromStdString(m_datasetPath.string());
    }
    setWindowTitle(tr("Amrvis2 — %1  T = %2  Levels: 0..%3  Finest Level: %3")
        .arg(name)
        .arg(metadata.time, 0, 'g', 12)
        .arg(metadata.finestLevel));
}

void MainWindow::chooseDataset()
{
    const auto settings = makeSettings();
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Open AMReX plotfile"),
        settings.value(QStringLiteral("lastOpenDirectory")).toString());
    if (!directory.isEmpty()) {
        openDataset(directory.toStdString());
    }
}

void MainWindow::chooseStandaloneDataset()
{
    const auto settings = makeSettings();
    const auto filename = QFileDialog::getOpenFileName(this,
        tr("Open standalone AMReX FAB or MultiFab header"),
        settings.value(QStringLiteral("lastOpenDirectory")).toString(),
        tr("AMReX data (*)"));
    if (!filename.isEmpty()) {
        openDataset(filename.toStdString());
    }
}

void MainWindow::exportImage()
{
    auto* view = m_activeView != nullptr ? m_activeView->view : nullptr;
    if (view == nullptr || !view->hasImage()) {
        QMessageBox::information(this, tr("No image"),
            tr("Open a dataset before exporting an image."));
        return;
    }
    const auto filename = QFileDialog::getSaveFileName(
        this, tr("Export scalar image"), QString(), tr("PNG image (*.png)"));
    if (!filename.isEmpty() && !view->image().save(filename, "PNG")) {
        QMessageBox::critical(this, tr("Cannot export image"),
            tr("The image could not be written to %1.").arg(filename));
    }
}

void MainWindow::exportSliceData()
{
    const auto* state = m_activeView;
    if (state == nullptr || state->plane.width <= 0 || state->plane.height <= 0) {
        QMessageBox::information(this, tr("No slice"),
            tr("Display a slice before exporting slice data."));
        return;
    }
    const auto filename = QFileDialog::getSaveFileName(this,
        tr("Export slice data (ASCII)"), QString(),
        tr("ASCII text (*.txt);;All files (*)"));
    if (filename.isEmpty()) {
        return;
    }

    const auto& plane = state->plane;
    const auto axes = displayAxes(state->normal);
    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto& region = plane.physicalRegion;
    constexpr std::array<const char*, 3> axisNames{"x", "y", "z"};

    std::ofstream output(filename.toStdString(), std::ios::trunc);
    if (!output) {
        QMessageBox::critical(this, tr("Cannot export slice data"),
            tr("The slice data could not be written to %1.").arg(filename));
        return;
    }
    output << "# Amrvis2 slice data export\n";
    output << "# dataset: " << m_datasetPath.string() << '\n';
    output << "# field: " << state->fieldName.toStdString() << '\n';
    if (m_viewDimension == 3) {
        const auto normal = static_cast<std::size_t>(state->normal);
        output << "# normal: " << axisNames[normal] << " (in-plane axes: "
            << axisNames[xAxis] << ' ' << axisNames[yAxis] << ")\n";
        output << "# slice position: " << axisNames[normal] << " = "
            << std::setprecision(12) << m_slicePosition3d[normal] << '\n';
    } else {
        output << "# normal: 2-D (in-plane axes: " << axisNames[xAxis] << ' '
            << axisNames[yAxis] << ")\n";
    }
    output << "# display range: [" << std::setprecision(9) << state->displayMinimum
        << ", " << state->displayMaximum << ']'
        << (state->displayLogarithmic ? " (log)\n" : "\n");
    output << "# columns: " << axisNames[xAxis] << ' ' << axisNames[yAxis]
        << " value valid sourceLevel\n";

    // Same pixel-center mapping the probe readout uses (see probeMoved);
    // plane row 0 is the bottom row of the displayed image.
    for (int y = 0; y < plane.height; ++y) {
        const auto physicalY = region.lower[yAxis]
            + (static_cast<double>(y) + 0.5) / static_cast<double>(plane.height)
                * (region.upper[yAxis] - region.lower[yAxis]);
        for (int x = 0; x < plane.width; ++x) {
            const auto physicalX = region.lower[xAxis]
                + (static_cast<double>(x) + 0.5) / static_cast<double>(plane.width)
                    * (region.upper[xAxis] - region.lower[xAxis]);
            const auto offset = static_cast<std::size_t>(x)
                + static_cast<std::size_t>(plane.width)
                    * static_cast<std::size_t>(y);
            output << std::setprecision(12) << physicalX << ' ' << physicalY << ' '
                << std::setprecision(9) << static_cast<double>(plane.values[offset])
                << ' ' << static_cast<int>(plane.valid[offset]) << ' '
                << plane.sourceLevel[offset] << '\n';
        }
    }
    output.flush();
    if (!output) {
        QMessageBox::critical(this, tr("Cannot export slice data"),
            tr("The slice data could not be written to %1.").arg(filename));
    }
}

std::optional<DatasetRequest> MainWindow::buildDatasetRequest() const
{
    if (!m_dataset || m_activeView == nullptr
        || m_activeView->plane.width <= 0 || m_activeView->plane.height <= 0
        || m_fieldSelector->currentIndex() < 0) {
        return std::nullopt;
    }
    const auto& metadata = m_dataset->metadata();
    DatasetRequest request;
    request.dataset = m_dataset;
    request.field.value = m_displayMode == DisplayMode::VelocityVectors
        ? static_cast<std::uint32_t>(std::max(m_vectorUField, 0))
        : m_fieldSelector->currentData().toUInt();
    request.fieldName = QString::fromStdString(
        metadata.fields[request.field.value].name);
    // The "selected region" is the active view's visible region: the
    // rubber-band zoom, or the whole domain when fitted.
    request.region = m_activeView->plane.physicalRegion;
    request.normalAxis = m_activeView->normal;
    if (metadata.dimension == 3) {
        request.slicePosition
            = m_slicePosition3d[static_cast<std::size_t>(m_activeView->normal)];
    }
    return request;
}

void MainWindow::showDatasetWindow()
{
    auto request = buildDatasetRequest();
    if (!request.has_value()) {
        return;
    }
    // One instance at a time: a new window replaces the old one.
    closeDatasetWindow();
    auto* window = new DatasetWindow(*request);
    window->setNumberFormat(m_numberFormat);
    m_datasetWindow = window;
    connect(window, &QObject::destroyed, this, [this, window] {
        if (m_datasetWindow == window) {
            m_datasetWindow = nullptr;
        }
        for (auto* state : currentViews()) {
            state->view->setCellHighlight(std::nullopt);
        }
    });
    connect(window, &DatasetWindow::cellActivated, this,
        [this](const RealBox& physicalCell) {
            datasetCellActivated(physicalCell);
        });
    connect(window, &DatasetWindow::refreshRequested, this,
        [this] { refreshDatasetWindow(); });
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::closeDatasetWindow()
{
    auto* window = m_datasetWindow;
    m_datasetWindow = nullptr;
    if (window != nullptr) {
        window->close();
    }
}

void MainWindow::refreshDatasetWindow()
{
    if (m_datasetWindow == nullptr) {
        return;
    }
    auto request = buildDatasetRequest();
    if (!request.has_value()) {
        closeDatasetWindow();
        return;
    }
    m_datasetWindow->reload(*request);
}

void MainWindow::datasetCellActivated(const RealBox& physicalCell)
{
    if (m_activeView == nullptr) {
        return;
    }
    const auto& plane = m_activeView->plane;
    if (plane.width <= 0 || plane.height <= 0) {
        return;
    }
    const auto axes = displayAxes(m_activeView->normal);
    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto& region = plane.physicalRegion;
    const auto xExtent = region.upper[xAxis] - region.lower[xAxis];
    const auto yExtent = region.upper[yAxis] - region.lower[yAxis];
    // Same physical-to-scene mapping updateGridBoxes applies; plane row 0 is
    // the image bottom, so scene y runs opposite to physical y.
    const auto pixelX0 = (physicalCell.lower[xAxis] - region.lower[xAxis])
        / xExtent * plane.width;
    const auto pixelX1 = (physicalCell.upper[xAxis] - region.lower[xAxis])
        / xExtent * plane.width;
    const auto pixelY0 = plane.height
        - (physicalCell.upper[yAxis] - region.lower[yAxis])
            / yExtent * plane.height;
    const auto pixelY1 = plane.height
        - (physicalCell.lower[yAxis] - region.lower[yAxis])
            / yExtent * plane.height;
    QRectF rectangle(QPointF(pixelX0, pixelY0), QPointF(pixelX1, pixelY1));
    rectangle = rectangle.normalized().intersected(
        QRectF(0.0, 0.0, plane.width, plane.height));
    std::optional<QRectF> highlight;
    if (!rectangle.isEmpty()) {
        highlight = rectangle;
    }
    m_activeView->view->setCellHighlight(highlight);
}

void MainWindow::openDataset(const std::filesystem::path& path, bool metadataOnly)
{
    // Opening a single dataset ends any plotfile sequence and stops playback
    // of either animation mode.
    setPlaybackMode(PlaybackMode::None);
    closeSequence();
    // Invalidate every in-flight per-view slice and reset the view states.
    const std::array<PlaneViewState*, 4> states{
        &m_view2d, &m_planeViews[0], &m_planeViews[1], &m_planeViews[2]};
    for (auto* state : states) {
        state->stopSource.request_stop();
        ++state->sliceGeneration;
        state->view->setPlaceholder(tr("Loading dataset..."));
        state->plane = {};
        state->contourPlane = {};
        state->contourFinePlane = {};
        state->contourFineFactor = 1;
        state->contourPolylines.clear();
        state->fieldName.clear();
        state->visibleRegion.reset();
        state->vectorSegments.clear();
        state->cachedRequest = {};
        state->hasCachedRequest = false;
        state->cachedMode = DisplayMode::Raster;
        state->cachedVectorVField = 0;
        state->cachedContourCount = 0;
    }
    m_initialStopSource.request_stop();
    m_linePlotStopSource.request_stop();
    m_pendingAllViews = false;
    m_pendingViews.clear();
    m_sliceDebounce->stop();
    m_controlsReady = false;
    m_viewDimension = 0;
    m_activeView = nullptr;
    m_dataset.reset();
    // Line plot curves are snapshots of this dataset; drop the window.
    auto* linePlotWindow = m_linePlotWindow;
    m_linePlotWindow = nullptr;
    if (linePlotWindow != nullptr) {
        linePlotWindow->close();
    }
    // The dataset window shows this dataset's raw values; drop it too.
    closeDatasetWindow();
    m_datasetPath = path;
    m_lastBlocksRead = 0;
    m_lastCacheHits = 0;
    m_lastPayloadBytesRead = 0;
    m_cacheBudgetBytes = 0;
    m_cacheResidentBytes = 0;
    m_cachePinnedBytes = 0;
    m_cacheEvictions = 0;
    m_fieldSelector->setEnabled(false);
    m_levelSelector->setEnabled(false);
    m_rangeMode->setEnabled(false);
    m_logarithmic->setEnabled(false);
    m_gridBoxes->setEnabled(false);
    m_rangeMinimum->setEnabled(false);
    m_rangeMaximum->setEnabled(false);
    m_slicePositionControls->setVisible(false);
    m_animationPanel->setSweepVisible(false);
    m_variableMenu->setEnabled(false);
    m_levelMenu->setEnabled(false);
    m_contoursAction->setEnabled(false);
    m_datasetAction->setEnabled(false);
    m_openMetadata.reset();
    m_fileVersion.clear();
    m_probeLines.clear();
    m_vectorUField = -1;
    m_vectorVField = -1;
    setWindowTitle(tr("Amrvis2"));
    {
        auto settings = makeSettings();
        settings.setValue(QStringLiteral("lastOpenDirectory"),
            QString::fromStdString(path.parent_path().string()));
    }
    m_probeLabel->clear();
    m_colorBar->clearRange();
    const auto generation = ++m_generation;
    ++m_activeRequests;
    statusBar()->showMessage(tr("Reading metadata for %1...").arg(
        QString::fromStdString(path.string())));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<PlotfileMetadataResult>(this);
    connect(watcher, &QFutureWatcher<PlotfileMetadataResult>::finished, this,
        [this, watcher, generation, path, metadataOnly] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation) {
                    showMetadata(result, path);
                    emit datasetOpenFinished(true);
                    if (!metadataOnly) {
                        requestInitialSlice(path, generation);
                    }
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception& error) {
                if (generation == m_generation) {
                    statusBar()->showMessage(tr("Dataset open failed"));
                    QMessageBox::critical(this, tr("Cannot open dataset"),
                        exceptionMessage(error));
                    emit datasetOpenFinished(false);
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([path] {
        return readDatasetMetadata(path);
    }));
}

void MainWindow::requestInitialSlice(
    const std::filesystem::path& path, std::uint64_t generation)
{
    validateVectorMode();
    const auto& metadata = *m_openMetadata;
    m_viewDimension = metadata.dimension;
    const auto views = currentViews();
    // The XY view starts out as the active one in 3-D.
    m_activeView = m_viewDimension == 3
        ? &m_planeViews[2] : &m_view2d;
    // Slice positions start at the domain midpoints (legacy behavior).
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto lower = metadata.physicalDomain.lower[axis];
        const auto upper = metadata.physicalDomain.upper[axis];
        m_slicePosition3d[axis] = lower + 0.5 * (upper - lower);
    }
    m_initialStopSource.request_stop();
    m_linePlotStopSource.request_stop();
    m_initialStopSource = std::stop_source{};
    const auto cancellation = m_initialStopSource.get_token();
    // The initial open uses default slice state: field 0, finest available,
    // visible range, linear scale, whole domain, midpoint positions.
    FrameSliceSpec spec;
    spec.palette = m_palette;
    spec.displayMode = m_displayMode;
    spec.vectorUField = static_cast<std::uint32_t>(std::max(m_vectorUField, 0));
    spec.vectorVField = static_cast<std::uint32_t>(std::max(m_vectorVField, 0));
    spec.contourCount = m_contourCount;
    // Per-view generations captured now: a view that gets a newer request
    // before the initial slices land keeps its newer data.
    std::vector<std::uint64_t> viewGenerations;
    viewGenerations.reserve(views.size());
    for (const auto* state : views) {
        viewGenerations.push_back(state->sliceGeneration);
    }
    ++m_activeRequests;
    statusBar()->showMessage(tr("Loading initial slice..."));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<InitialSliceResult>(this);
    connect(watcher, &QFutureWatcher<InitialSliceResult>::finished, this,
        [this, watcher, generation, cancellation, views, viewGenerations] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation) {
                    m_dataset = result.dataset;
                    configureSliceControls();
                    if (result.displays.size() != views.size()) {
                        throw std::runtime_error(
                            "initial slice count does not match the view set");
                    }
                    for (std::size_t index = 0; index < views.size(); ++index) {
                        if (views[index]->sliceGeneration
                            != viewGenerations[index]) {
                            continue;
                        }
                        showSlice(*views[index], result.displays[index]);
                    }
                    const auto cache = m_dataset->cacheMetrics();
                    m_cacheBudgetBytes = cache.budgetBytes;
                    m_cacheResidentBytes = cache.residentBytes;
                    m_cachePinnedBytes = cache.pinnedBytes;
                    m_cacheEvictions = cache.evictions;
                    emit initialSliceFinished(true);
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception& error) {
                if (generation == m_generation && !cancellation.stop_requested()) {
                    statusBar()->showMessage(tr("Initial slice failed"));
                    qWarning("initial slice failed: %s",
                        qUtf8Printable(exceptionMessage(error)));
                    QMessageBox::critical(this, tr("Cannot load slice"),
                        exceptionMessage(error));
                    emit initialSliceFinished(false);
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [path, generation, spec = std::move(spec), cancellation] {
        return executeFrameLoad(path, DatasetId{generation}, spec, cancellation);
    }));
}

void MainWindow::configureSliceControls()
{
    if (!m_dataset) {
        return;
    }
    const QSignalBlocker fieldBlocker(m_fieldSelector);
    const QSignalBlocker levelBlocker(m_levelSelector);
    const auto& metadata = m_dataset->metadata();

    m_fieldSelector->clear();
    for (std::size_t field = 0; field < metadata.fields.size(); ++field) {
        m_fieldSelector->addItem(QString::fromStdString(metadata.fields[field].name),
            static_cast<unsigned int>(field));
    }
    m_fieldSelector->setCurrentIndex(0);

    m_levelSelector->clear();
    m_levelSelector->addItem(tr("Finest available"), -1);
    for (int level = 0; level <= metadata.finestLevel; ++level) {
        m_levelSelector->addItem(tr("Level %1 only").arg(level), level);
    }
    m_levelSelector->setCurrentIndex(0);

    m_controlsReady = true;
    m_fieldSelector->setEnabled(true);
    m_levelSelector->setEnabled(true);
    m_rangeMode->setEnabled(true);
    m_logarithmic->setEnabled(true);
    m_gridBoxes->setEnabled(true);
    const auto userRange = static_cast<RangeMode>(
        m_rangeMode->currentData().toInt()) == RangeMode::User;
    m_rangeMinimum->setEnabled(userRange);
    m_rangeMaximum->setEnabled(userRange);
    rebuildVariableMenu();
    rebuildLevelMenu();
    m_variableMenu->setEnabled(true);
    m_levelMenu->setEnabled(true);
    m_contoursAction->setEnabled(true);
    m_datasetAction->setEnabled(true);

    // Switch the stacked page to match the dataset dimension and, for 3-D,
    // reveal the shared slice position controls and the iso wireframe.
    const auto isThreeDimensional = metadata.dimension == 3;
    m_stack->setCurrentIndex(isThreeDimensional ? 1 : 0);
    m_animationPanel->setSweepVisible(isThreeDimensional);
    updateAnimationDockVisibility();
    configureSlicePositionControls();
    if (isThreeDimensional) {
        m_isoWidget->setGeometry(metadata);
        m_isoWidget->setSlicePositions(m_slicePosition3d[0], m_slicePosition3d[1],
            m_slicePosition3d[2]);
    }
    ensureVectorFieldDefaults();
}

void MainWindow::configureSlicePositionControls()
{
    if (!m_dataset || m_dataset->metadata().dimension != 3) {
        m_slicePositionControls->setVisible(false);
        return;
    }
    const auto& metadata = m_dataset->metadata();
    const auto& domain = metadata.physicalDomain;
    const auto& cellSize = metadata.levels.back().cellSize;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        auto* spin = m_sliceSpinboxes[axis];
        const QSignalBlocker blocker(spin);
        spin->setRange(domain.lower[axis],
            std::nextafter(domain.upper[axis], domain.lower[axis]));
        spin->setSingleStep(cellSize[axis]);
        spin->setValue(m_slicePosition3d[axis]);
    }
    m_slicePositionControls->setVisible(true);
}

void MainWindow::setSlicePosition(int axis, double value)
{
    if (!m_dataset || m_dataset->metadata().dimension != 3) {
        return;
    }
    const auto index = static_cast<std::size_t>(axis);
    const auto& domain = m_dataset->metadata().physicalDomain;
    const auto position = std::clamp(value, domain.lower[index],
        std::nextafter(domain.upper[index], domain.lower[index]));
    m_slicePosition3d[index] = position;
    {
        const QSignalBlocker blocker(m_sliceSpinboxes[index]);
        m_sliceSpinboxes[index]->setValue(position);
    }
    m_isoWidget->setSlicePositions(m_slicePosition3d[0], m_slicePosition3d[1],
        m_slicePosition3d[2]);
    // The other two views only need their crosshair guides redrawn; the view
    // normal to the moved axis gets a fresh (debounced) slice.
    updateCrosshairs();
    scheduleSliceRequest(m_planeViews[index]);
}

void MainWindow::scheduleSliceRequest(bool rasterDirty)
{
    if (m_controlsReady && m_dataset) {
        // Any slice-affecting UI change funnels through here; a prefetched
        // frame rendered against the old spec is obsolete.
        ++m_specGeneration;
        discardPrefetch();
        m_pendingRasterDirty = m_pendingRasterDirty || rasterDirty;
        m_pendingAllViews = true;
        m_sliceDebounce->start();
    }
}

void MainWindow::scheduleSliceRequest(PlaneViewState& state, bool rasterDirty)
{
    if (m_controlsReady && m_dataset) {
        ++m_specGeneration;
        discardPrefetch();
        m_pendingRasterDirty = m_pendingRasterDirty || rasterDirty;
        if (std::find(m_pendingViews.begin(), m_pendingViews.end(), &state)
            == m_pendingViews.end()) {
            m_pendingViews.push_back(&state);
        }
        m_sliceDebounce->start();
    }
}

void MainWindow::flushSliceRequests()
{
    std::vector<PlaneViewState*> targets;
    if (m_pendingAllViews) {
        targets = currentViews();
    } else {
        targets = m_pendingViews;
    }
    m_pendingAllViews = false;
    m_pendingViews.clear();
    const auto rasterDirty = m_pendingRasterDirty;
    m_pendingRasterDirty = false;
    for (auto* state : targets) {
        requestSlice(*state, rasterDirty);
    }
}

void MainWindow::requestSlice(PlaneViewState& state, bool rasterDirty)
{
    if (!m_controlsReady || !m_dataset
        || m_fieldSelector->currentIndex() < 0
        || m_levelSelector->currentIndex() < 0) {
        return;
    }

    const auto dataset = m_dataset;
    const auto& metadata = dataset->metadata();
    SliceRequest request;
    request.dataset = dataset->id();
    request.field.value = m_displayMode == DisplayMode::VelocityVectors
        ? static_cast<std::uint32_t>(std::max(m_vectorUField, 0))
        : m_fieldSelector->currentData().toUInt();
    request.normalDirection = state.normal;
    if (metadata.dimension == 3) {
        request.physicalPosition
            = m_slicePosition3d[static_cast<std::size_t>(state.normal)];
    }
    request.visibleRegion = state.visibleRegion.value_or(metadata.physicalDomain);
    request.outputSize = finestNativeOutputSize(
        metadata, request.visibleRegion, state.normal);
    const auto level = m_levelSelector->currentData().toInt();
    request.composition = level < 0
        ? CompositionPolicy::FinestAvailable : CompositionPolicy::ExactLevel;
    request.maximumLevel = level < 0 ? metadata.finestLevel : level;

    const auto rangeMode = static_cast<RangeMode>(m_rangeMode->currentData().toInt());
    std::optional<std::pair<double, double>> userRange;
    if (rangeMode == RangeMode::User) {
        userRange = std::pair{m_rangeMinimum->value(), m_rangeMaximum->value()};
    }
    const auto logarithmic = m_logarithmic->isChecked();
    const auto palette = m_palette;
    const auto displayMode = m_displayMode;
    const auto vectorVField = static_cast<std::uint32_t>(
        std::max(m_vectorVField, 0));
    const auto contourCount = m_contourCount;

    // Everything the cached planes depend on is unchanged (the request
    // matches the cache key, and the mode-specific companions are cached):
    // satisfy the request from the cached planes instead of querying again.
    // Vector mode falls back to the full path when the glyph count changed,
    // because glyphs are baked into the cached slice.
    const auto fromCache = state.hasCachedRequest
        && state.plane.width > 0
        && sameSliceSpec(state.cachedRequest, request)
        && state.cachedVectorVField == vectorVField
        && (!isContourMode(displayMode) || state.contourFinePlane.width > 0)
        && (displayMode != DisplayMode::VelocityVectors
            || (state.cachedMode == DisplayMode::VelocityVectors
                && contourCount == state.cachedContourCount));

    state.stopSource.request_stop();
    state.stopSource = std::stop_source{};
    const auto cancellation = state.stopSource.get_token();
    const auto generation = m_generation;
    const auto sliceGeneration = ++state.sliceGeneration;
    ++state.pendingRequests;
    ++m_activeRequests;
    const auto tag = m_viewDimension == 3
        ? tr(" (%1)").arg(state.label) : QString();
    statusBar()->showMessage(tr("Loading %1%2...").arg(
        m_fieldSelector->currentText(), tag));
    updateDiagnostics();

    QFuture<SliceDisplayResult> future;
    if (fromCache) {
        // Cheap path: re-range, re-render, and re-contour the cached planes
        // on a worker; no SliceQuery runs at all.
        future = QtConcurrent::run([dataset, request,
            displayPlane = state.plane,
            contourPlane = state.contourPlane,
            contourFinePlane = state.contourFinePlane,
            contourFineFactor = state.contourFineFactor,
            vectors = state.vectorSegments,
            rangeMode, userRange, logarithmic, palette, displayMode,
            vectorVField, contourCount, rasterDirty]() mutable {
            return refreshCachedSlice(dataset, request, std::move(displayPlane),
                std::move(contourPlane), std::move(contourFinePlane),
                contourFineFactor, std::move(vectors), rangeMode, userRange,
                logarithmic, palette, displayMode, vectorVField, contourCount,
                rasterDirty);
        });
    } else {
        future = QtConcurrent::run(
            [dataset, request, rangeMode, userRange, logarithmic, palette,
                cancellation, displayMode, vectorVField, contourCount] {
            auto result = executeSlice(dataset, request, rangeMode,
                userRange, logarithmic, palette, cancellation);
            result.mode = displayMode;
            result.vectorVField = vectorVField;
            result.contourCount = contourCount;
            if (isContourMode(displayMode)) {
                appendContours(dataset, request, contourCount, result.minimum,
                    result.maximum, cancellation, result);
            }
            if (displayMode == DisplayMode::VelocityVectors) {
                appendVectorGlyphs(dataset, request, FieldId{vectorVField},
                    contourCount, cancellation, result);
            }
            return result;
        });
    }

    auto* watcher = new QFutureWatcher<SliceDisplayResult>(this);
    connect(watcher, &QFutureWatcher<SliceDisplayResult>::finished, this,
        [this, watcher, dataset, generation, sliceGeneration, cancellation, &state] {
            --state.pendingRequests;
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation
                    && sliceGeneration == state.sliceGeneration) {
                    showSlice(state, result);
                    const auto cache = dataset->cacheMetrics();
                    m_cacheBudgetBytes = cache.budgetBytes;
                    m_cacheResidentBytes = cache.residentBytes;
                    m_cachePinnedBytes = cache.pinnedBytes;
                    m_cacheEvictions = cache.evictions;
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception& error) {
                if (generation == m_generation
                    && sliceGeneration == state.sliceGeneration
                    && !cancellation.stop_requested()) {
                    statusBar()->showMessage(tr("Slice request failed"));
                    QMessageBox::critical(this, tr("Cannot load slice"),
                        exceptionMessage(error));
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(future);
}

void MainWindow::updateGridBoxes(PlaneViewState& state)
{
    std::vector<GridBoxOverlay> overlays;
    if (!m_gridBoxes->isChecked() || !m_dataset || !state.view->hasImage()
        || state.plane.width <= 0 || state.plane.height <= 0) {
        state.view->setGridBoxes(overlays);
        return;
    }

    const auto& metadata = m_dataset->metadata();
    const auto& plane = state.plane;
    const auto normal = metadata.dimension == 3 ? state.normal : -1;
    const auto axes = displayAxes(state.normal);
    const auto selectedLevel = m_levelSelector->currentData().toInt();
    const auto firstLevel = selectedLevel < 0 ? 0 : selectedLevel;
    const auto lastLevel = selectedLevel < 0
        ? metadata.finestLevel : selectedLevel;

    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto xExtent = plane.physicalRegion.upper[xAxis]
        - plane.physicalRegion.lower[xAxis];
    const auto yExtent = plane.physicalRegion.upper[yAxis]
        - plane.physicalRegion.lower[yAxis];
    for (int levelIndex = firstLevel; levelIndex <= lastLevel; ++levelIndex) {
        const auto& level = metadata.levels[static_cast<std::size_t>(levelIndex)];
        for (const auto& box : level.boxes) {
            if (normal >= 0) {
                // Only boxes intersecting this view's slice position show.
                const auto direction = static_cast<std::size_t>(normal);
                const auto normalLower = metadata.physicalDomain.lower[direction]
                    + static_cast<double>(static_cast<std::int64_t>(box.lower[direction])
                        - level.domain.lower[direction]) * level.cellSize[direction];
                const auto normalUpper = metadata.physicalDomain.lower[direction]
                    + static_cast<double>(static_cast<std::int64_t>(box.upper[direction])
                        - level.domain.lower[direction] + 1) * level.cellSize[direction];
                const auto slicePosition
                    = m_slicePosition3d[static_cast<std::size_t>(normal)];
                if (slicePosition < normalLower || slicePosition >= normalUpper) {
                    continue;
                }
            }

            const auto xLower = metadata.physicalDomain.lower[xAxis]
                + static_cast<double>(static_cast<std::int64_t>(box.lower[xAxis])
                    - level.domain.lower[xAxis]) * level.cellSize[xAxis];
            const auto xUpper = metadata.physicalDomain.lower[xAxis]
                + static_cast<double>(static_cast<std::int64_t>(box.upper[xAxis])
                    - level.domain.lower[xAxis] + 1) * level.cellSize[xAxis];
            const auto yLower = metadata.physicalDomain.lower[yAxis]
                + static_cast<double>(static_cast<std::int64_t>(box.lower[yAxis])
                    - level.domain.lower[yAxis]) * level.cellSize[yAxis];
            const auto yUpper = metadata.physicalDomain.lower[yAxis]
                + static_cast<double>(static_cast<std::int64_t>(box.upper[yAxis])
                    - level.domain.lower[yAxis] + 1) * level.cellSize[yAxis];
            const auto pixelX0 = (xLower - plane.physicalRegion.lower[xAxis])
                / xExtent * plane.width;
            const auto pixelX1 = (xUpper - plane.physicalRegion.lower[xAxis])
                / xExtent * plane.width;
            const auto pixelY0 = plane.height
                - (yUpper - plane.physicalRegion.lower[yAxis])
                    / yExtent * plane.height;
            const auto pixelY1 = plane.height
                - (yLower - plane.physicalRegion.lower[yAxis])
                    / yExtent * plane.height;
            QRectF rectangle(QPointF(pixelX0, pixelY0), QPointF(pixelX1, pixelY1));
            rectangle = rectangle.normalized().intersected(
                QRectF(0.0, 0.0, plane.width, plane.height));
            if (!rectangle.isEmpty()) {
                const auto color = levelIndex == firstLevel
                    ? QColor(Qt::white)
                    : QColor::fromRgb(static_cast<QRgb>(
                        m_palette.levelColor(levelIndex, lastLevel)));
                overlays.push_back({rectangle, color});
            }
        }
    }
    state.view->setGridBoxes(overlays);
}

void MainWindow::updateGridBoxes()
{
    for (auto* state : currentViews()) {
        updateGridBoxes(*state);
    }
}

void MainWindow::updateCrosshairs(PlaneViewState& state)
{
    std::optional<QLineF> vertical;
    std::optional<QLineF> horizontal;
    QColor verticalColor;
    QColor horizontalColor;
    if (m_dataset && m_dataset->metadata().dimension == 3
        && state.plane.width > 0 && state.plane.height > 0) {
        const auto axes = displayAxes(state.normal);
        const auto xAxis = static_cast<std::size_t>(axes[0]);
        const auto yAxis = static_cast<std::size_t>(axes[1]);
        const auto& region = state.plane.physicalRegion;
        const auto width = static_cast<double>(state.plane.width);
        const auto height = static_cast<double>(state.plane.height);
        // The vertical guide marks the slice position of the axis pointing
        // horizontally in this view, and vice versa; each guide takes that
        // axis' legacy palette color and hides outside the displayed region.
        const auto xPosition = m_slicePosition3d[xAxis];
        if (xPosition >= region.lower[xAxis] && xPosition <= region.upper[xAxis]) {
            const auto t = (xPosition - region.lower[xAxis])
                / (region.upper[xAxis] - region.lower[xAxis]);
            vertical = QLineF(t * width, 0.0, t * width, height);
            verticalColor = sliceAxisColor(axes[0]);
        }
        const auto yPosition = m_slicePosition3d[yAxis];
        if (yPosition >= region.lower[yAxis] && yPosition <= region.upper[yAxis]) {
            const auto t = (yPosition - region.lower[yAxis])
                / (region.upper[yAxis] - region.lower[yAxis]);
            const auto sceneY = height * (1.0 - t);
            horizontal = QLineF(0.0, sceneY, width, sceneY);
            horizontalColor = sliceAxisColor(axes[1]);
        }
    }
    state.view->setCrosshairs(vertical, horizontal, verticalColor,
        horizontalColor);
}

void MainWindow::updateCrosshairs()
{
    for (auto* state : currentViews()) {
        updateCrosshairs(*state);
    }
}

void MainWindow::showMetadata(
    const PlotfileMetadataResult& result, const std::filesystem::path& path)
{
    m_metadataTree->clear();
    const auto& metadata = *result.metadata;
    const auto addValue = [this](const QString& name, const QString& value) {
        new QTreeWidgetItem(m_metadataTree, {name, value});
    };

    addValue(tr("Dataset"), QString::fromStdString(path.string()));
    addValue(tr("Format"), QString::fromStdString(result.fileVersion));
    addValue(tr("Dimension"), QString::number(metadata.dimension));
    addValue(tr("Time"), QString::number(metadata.time, 'g', 17));
    addValue(tr("Finest level"), QString::number(metadata.finestLevel));

    auto* fields = new QTreeWidgetItem(
        m_metadataTree, {tr("Fields"), QString::number(metadata.fields.size())});
    for (const auto& field : metadata.fields) {
        new QTreeWidgetItem(fields, {
            QString::fromStdString(field.name),
            tr("%1 component(s)").arg(field.componentCount)
        });
    }

    auto* levels = new QTreeWidgetItem(
        m_metadataTree, {tr("Levels"), QString::number(metadata.levels.size())});
    for (const auto& level : metadata.levels) {
        new QTreeWidgetItem(levels, {
            tr("Level %1").arg(level.level),
            tr("%1 grid(s), %2").arg(level.boxes.size()).arg(
                QString::fromStdString(level.dataPath))
        });
    }
    m_metadataTree->expandAll();

    m_openMetadata = result.metadata;
    m_fileVersion = result.fileVersion;
    updateWindowTitle();

    m_lastFilesRead = result.metrics.filesRead;
    m_lastBytesRead = result.metrics.bytesRead;
    statusBar()->showMessage(tr("Metadata loaded: %1 field(s), %2 level(s)")
        .arg(metadata.fields.size()).arg(metadata.levels.size()));
}

void MainWindow::showSlice(PlaneViewState& state, const SliceDisplayResult& display)
{
    if (!display.rasterUnchanged) {
        if (!display.image.valid()) {
            throw std::runtime_error("renderer produced an invalid image");
        }
        const QImage wrapped(
            reinterpret_cast<const uchar*>(display.image.rgba.data()),
            display.image.width, display.image.height, display.image.strideBytes,
            QImage::Format_ARGB32);
        const auto displayImage = wrapped.mirrored(false, true).copy();
        state.view->setImage(displayImage);
    }
    state.plane = display.slice.plane;
    state.contourPlane = display.contourPlane;
    state.contourFinePlane = display.contourFinePlane;
    state.contourFineFactor = display.contourFineFactor;
    state.contourPolylines = display.contourPolylines;
    const auto fieldName = QString::fromStdString(display.fieldName);
    state.fieldName = fieldName;
    state.displayMinimum = display.minimum;
    state.displayMaximum = display.maximum;
    state.displayLogarithmic = display.logarithmic;
    state.vectorSegments = display.vectors;
    // Cache key for the re-render-from-cache path (see requestSlice).
    state.cachedRequest = display.request;
    state.hasCachedRequest = true;
    state.cachedMode = display.mode;
    state.cachedVectorVField = display.vectorVField;
    state.cachedContourCount = display.contourCount;
    if (m_activeView == &state) {
        m_colorBar->setLogarithmic(display.logarithmic);
        m_colorBar->setFieldRange(
            display.logarithmic ? fieldName + tr(" (log)") : fieldName,
            display.minimum, display.maximum);
        // If log was requested but fell back to linear, reflect that in the
        // checkbox so the user sees log did not apply.
        if (m_logarithmic->isChecked() != display.logarithmic) {
            const QSignalBlocker logarithmicBlocker(m_logarithmic);
            m_logarithmic->setChecked(display.logarithmic);
        }
        if (static_cast<RangeMode>(m_rangeMode->currentData().toInt())
            != RangeMode::User) {
            const QSignalBlocker minimumBlocker(m_rangeMinimum);
            const QSignalBlocker maximumBlocker(m_rangeMaximum);
            m_rangeMinimum->setValue(display.minimum);
            m_rangeMaximum->setValue(display.maximum);
        }
    }
    updateGridBoxes(state);
    updateOverlay(state);
    // This view's region may have changed; refresh every view's guides.
    updateCrosshairs();

    m_lastBlocksRead = display.slice.metrics.blocksRead;
    m_lastCacheHits = display.slice.metrics.cacheHits;
    m_lastPayloadBytesRead = display.slice.metrics.payloadBytesRead;
    statusBar()->clearMessage();
}

void MainWindow::choosePlotfileSequence()
{
    const auto settings = makeSettings();
    const auto filenames = QFileDialog::getOpenFileNames(this,
        tr("Open Plotfile Sequence — select two or more plotfile Header files"),
        settings.value(QStringLiteral("lastOpenDirectory")).toString(),
        tr("AMReX plotfile Header (Header);;All files (*)"));
    if (filenames.isEmpty()) {
        return;
    }
    std::vector<std::filesystem::path> frames;
    frames.reserve(static_cast<std::size_t>(filenames.size()));
    for (const auto& filename : filenames) {
        frames.push_back(
            std::filesystem::path(filename.toStdString()).parent_path());
    }
    auto writableSettings = makeSettings();
    writableSettings.setValue(QStringLiteral("lastOpenDirectory"),
        QFileInfo(filenames.first()).absolutePath());
    openSequence(frames);
}

void MainWindow::openSequence(const std::vector<std::filesystem::path>& frames)
{
    // Sweep and sequence playback are mutually exclusive.
    setPlaybackMode(PlaybackMode::None);
    closeSequence();

    auto sorted = frames;
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.filename() < rhs.filename();
        });
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const auto valid = std::all_of(sorted.begin(), sorted.end(),
        [](const auto& frame) {
            std::error_code error;
            return std::filesystem::is_regular_file(frame / "Header", error);
        });
    if (sorted.size() < 2 || !valid) {
        emit sequenceFrameFailed();
        QMessageBox::warning(this, tr("Cannot open sequence"),
            tr("Select two or more plotfile Header files, each inside its own "
               "plotfile directory."));
        return;
    }

    m_sequenceFrames = std::move(sorted);
    m_animationPanel->setSequenceFrameCount(
        static_cast<int>(m_sequenceFrames.size()));
    m_animationPanel->setSequenceVisible(true);
    updateAnimationDockVisibility();
    // Line plot curves are snapshots of the previous dataset; drop the window.
    auto* linePlotWindow = m_linePlotWindow;
    m_linePlotWindow = nullptr;
    if (linePlotWindow != nullptr) {
        linePlotWindow->close();
    }
    goToSequenceFrame(0);
}

void MainWindow::closeSequence()
{
    if (m_playbackMode == PlaybackMode::Sequence) {
        setPlaybackMode(PlaybackMode::None);
    }
    discardPrefetch();
    m_sequenceFrames.clear();
    m_sequenceIndex = -1;
    m_sequenceInFlight = false;
    m_animationPanel->setSequenceVisible(false);
    updateAnimationDockVisibility();
}

void MainWindow::updateAnimationDockVisibility()
{
    // The Animation panel hosts the 3-D slice-sweep controls and the
    // plotfile-sequence controls. Keep it visible only when one of those
    // applies; otherwise it is dead space.
    const auto sequenceActive = !m_sequenceFrames.empty();
    const auto threeD = m_dataset != nullptr
        && m_dataset->metadata().dimension == 3;
    m_animationDock->setVisible(sequenceActive || threeD);
}

void MainWindow::stepSequence(int direction)
{
    if (m_sequenceFrames.empty()) {
        return;
    }
    goToSequenceFrame(m_sequenceIndex + direction);
}

void MainWindow::goToSequenceFrame(int index)
{
    if (m_sequenceFrames.empty()) {
        return;
    }
    const auto count = static_cast<int>(m_sequenceFrames.size());
    // Both steps and playback wrap around the ends of the sequence.
    index = ((index % count) + count) % count;
    if (m_sequenceInFlight && index == m_sequenceIndex) {
        return;
    }
    // Cancel the current dataset's in-flight work, exactly like opening a
    // fresh dataset does, but keep the view state (field, level, range,
    // log, palette, zoom, slice positions) for the next frame.
    const std::array<PlaneViewState*, 4> states{
        &m_view2d, &m_planeViews[0], &m_planeViews[1], &m_planeViews[2]};
    for (auto* state : states) {
        state->stopSource.request_stop();
        ++state->sliceGeneration;
    }
    m_initialStopSource.request_stop();
    m_linePlotStopSource.request_stop();
    m_pendingAllViews = false;
    m_pendingViews.clear();
    m_sliceDebounce->stop();
    // The dataset window shows the previous frame's raw values; drop it.
    closeDatasetWindow();
    const auto generation = ++m_generation;
    m_sequenceIndex = index;
    m_sequenceInFlight = true;
    m_frameTimer.start();
    m_datasetPath = m_sequenceFrames[static_cast<std::size_t>(index)];
    m_animationPanel->setSequenceFrame(index);

    // A still-valid prefetch of this frame is consumed instead of loading
    // again; anything else in the slot is cancelled and dropped.
    if (m_prefetched && m_prefetched->frameIndex == index
        && m_prefetched->specGeneration == m_specGeneration) {
        auto prefetched = std::move(*m_prefetched);
        m_prefetched.reset();
        discardPrefetch();
        finishFrameLoad(std::move(prefetched.result), prefetched.defaultPositions);
        return;
    }
    discardPrefetch();
    startFrameLoad(index, generation);
}

void MainWindow::startFrameLoad(int index, std::uint64_t generation)
{
    auto spec = buildFrameSpec();
    const auto defaultPositions = spec.defaultPositions;
    const auto path = m_sequenceFrames[static_cast<std::size_t>(index)];
    const auto datasetId = DatasetId{
        sequenceDatasetIdBase + ++m_sequenceDatasetCounter};
    m_initialStopSource = std::stop_source{};
    const auto cancellation = m_initialStopSource.get_token();
    ++m_activeRequests;
    statusBar()->showMessage(tr("Loading frame %1...").arg(
        QString::fromStdString(path.filename().string())));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<InitialSliceResult>(this);
    connect(watcher, &QFutureWatcher<InitialSliceResult>::finished, this,
        [this, watcher, generation, index, defaultPositions] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation && index == m_sequenceIndex) {
                    finishFrameLoad(std::move(result), defaultPositions);
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception& error) {
                if (generation == m_generation && index == m_sequenceIndex) {
                    m_sequenceInFlight = false;
                    statusBar()->showMessage(tr("Frame load failed"));
                    emit sequenceFrameFailed();
                    QMessageBox::critical(this, tr("Cannot load frame"),
                        exceptionMessage(error));
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [path, datasetId, spec = std::move(spec), cancellation] {
        return executeFrameLoad(path, datasetId, spec, cancellation);
    }));
}

void MainWindow::finishFrameLoad(InitialSliceResult result, bool defaultPositions)
{
    try {
        displayFrameResult(result, defaultPositions);
    } catch (const std::exception& error) {
        m_sequenceInFlight = false;
        statusBar()->showMessage(tr("Frame load failed"));
        emit sequenceFrameFailed();
        QMessageBox::critical(this, tr("Cannot load frame"),
            exceptionMessage(error));
        updateDiagnostics();
        return;
    }
    m_sequenceInFlight = false;
    m_lastFrameSwitchMs = m_frameTimer.elapsed();
    m_animationPanel->setSequenceFrame(m_sequenceIndex);
    m_animationPanel->setSequenceInfo(
        QString::fromStdString(m_datasetPath.filename().string()),
        m_openMetadata->time);
    updateDiagnostics();
    emit sequenceFrameDisplayed(m_sequenceIndex);
    // Bounded low-priority prefetch of the next frame: queued behind the
    // display update, and re-validated when it runs so a frame jump in the
    // meantime does not start obsolete I/O.
    const auto displayedIndex = m_sequenceIndex;
    QTimer::singleShot(0, this, [this, displayedIndex] {
        if (m_sequenceFrames.empty() || m_sequenceInFlight
            || m_sequenceIndex != displayedIndex) {
            return;
        }
        const auto count = static_cast<int>(m_sequenceFrames.size());
        startPrefetch((displayedIndex + 1) % count);
    });
}

void MainWindow::displayFrameResult(InitialSliceResult& result,
    bool defaultPositions)
{
    m_dataset = result.dataset;
    const auto& metadata = m_dataset->metadata();
    m_viewDimension = metadata.dimension;

    // Refresh the metadata dock and the window title (frame name + time).
    PlotfileMetadataResult frameMetadata;
    frameMetadata.metadata = std::make_shared<DatasetMetadata>(metadata);
    frameMetadata.metrics = result.dataset->metadataReadMetrics();
    frameMetadata.fileVersion = !result.fileVersion.empty()
        ? result.fileVersion : m_fileVersion;
    showMetadata(frameMetadata, m_datasetPath);

    configureSequenceControls(defaultPositions);
    const auto views = currentViews();
    if (result.displays.size() != views.size()) {
        throw std::runtime_error("frame slice count does not match the view set");
    }
    for (std::size_t index = 0; index < views.size(); ++index) {
        showSlice(*views[index], result.displays[index]);
    }
    const auto cache = m_dataset->cacheMetrics();
    m_cacheBudgetBytes = cache.budgetBytes;
    m_cacheResidentBytes = cache.residentBytes;
    m_cachePinnedBytes = cache.pinnedBytes;
    m_cacheEvictions = cache.evictions;
    validateVectorMode();
}

void MainWindow::configureSequenceControls(bool defaultPositions)
{
    if (!m_dataset) {
        return;
    }
    const auto& metadata = m_dataset->metadata();
    // Preserve the user's selections across frames: the field index if it
    // still exists, the level by its combo data (falling back to finest
    // available when this frame has fewer levels).
    const auto previousField = m_controlsReady && m_fieldSelector->count() > 0
        ? m_fieldSelector->currentIndex() : 0;
    const auto previousLevel = m_controlsReady
        && m_levelSelector->currentIndex() >= 0
            ? m_levelSelector->currentData().toInt() : -1;
    {
        const QSignalBlocker fieldBlocker(m_fieldSelector);
        const QSignalBlocker levelBlocker(m_levelSelector);
        m_fieldSelector->clear();
        for (std::size_t field = 0; field < metadata.fields.size(); ++field) {
            m_fieldSelector->addItem(
                QString::fromStdString(metadata.fields[field].name),
                static_cast<unsigned int>(field));
        }
        m_fieldSelector->setCurrentIndex(
            std::clamp(previousField, 0, m_fieldSelector->count() - 1));
        m_levelSelector->clear();
        m_levelSelector->addItem(tr("Finest available"), -1);
        for (int level = 0; level <= metadata.finestLevel; ++level) {
            m_levelSelector->addItem(tr("Level %1 only").arg(level), level);
        }
        const auto levelIndex = m_levelSelector->findData(previousLevel);
        m_levelSelector->setCurrentIndex(levelIndex >= 0 ? levelIndex : 0);
    }

    // 3-D keeps the user's slice positions (clamped into the new domain);
    // the first 3-D frame of a session starts at the domain midpoints.
    const auto isThreeDimensional = metadata.dimension == 3;
    if (isThreeDimensional) {
        const auto& domain = metadata.physicalDomain;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            m_slicePosition3d[axis] = defaultPositions
                ? domain.lower[axis]
                    + 0.5 * (domain.upper[axis] - domain.lower[axis])
                : std::clamp(m_slicePosition3d[axis], domain.lower[axis],
                    std::nextafter(domain.upper[axis], domain.lower[axis]));
        }
        m_isoWidget->setGeometry(metadata);
        m_isoWidget->setSlicePositions(m_slicePosition3d[0], m_slicePosition3d[1],
            m_slicePosition3d[2]);
    }
    m_stack->setCurrentIndex(isThreeDimensional ? 1 : 0);
    m_animationPanel->setSweepVisible(isThreeDimensional);
    updateAnimationDockVisibility();
    configureSlicePositionControls();

    // The active view must belong to the new dimension's view set.
    const auto views = currentViews();
    if (std::find(views.begin(), views.end(), m_activeView) == views.end()) {
        m_activeView = isThreeDimensional ? &m_planeViews[2] : &m_view2d;
    }

    m_controlsReady = true;
    m_fieldSelector->setEnabled(true);
    m_levelSelector->setEnabled(true);
    m_rangeMode->setEnabled(true);
    m_logarithmic->setEnabled(true);
    m_gridBoxes->setEnabled(true);
    const auto userRange = static_cast<RangeMode>(
        m_rangeMode->currentData().toInt()) == RangeMode::User;
    m_rangeMinimum->setEnabled(userRange);
    m_rangeMaximum->setEnabled(userRange);
    rebuildVariableMenu();
    rebuildLevelMenu();
    m_variableMenu->setEnabled(true);
    m_levelMenu->setEnabled(true);
    m_contoursAction->setEnabled(true);
    m_datasetAction->setEnabled(true);
    ensureVectorFieldDefaults();
}

FrameSliceSpec MainWindow::buildFrameSpec()
{
    FrameSliceSpec spec;
    spec.displayMode = m_displayMode;
    spec.palette = m_palette;
    spec.contourCount = m_contourCount;
    spec.logarithmic = m_logarithmic->isChecked();
    spec.rangeMode = static_cast<RangeMode>(m_rangeMode->currentData().toInt());
    if (spec.rangeMode == RangeMode::User) {
        spec.userRange = std::pair{m_rangeMinimum->value(),
            m_rangeMaximum->value()};
    }
    spec.field = m_controlsReady && m_fieldSelector->currentIndex() >= 0
        ? m_fieldSelector->currentData().toUInt() : 0U;
    spec.levelSelection = m_controlsReady && m_levelSelector->currentIndex() >= 0
        ? m_levelSelector->currentData().toInt() : -1;
    spec.vectorUField = static_cast<std::uint32_t>(std::max(m_vectorUField, 0));
    spec.vectorVField = static_cast<std::uint32_t>(std::max(m_vectorVField, 0));
    // Slice positions only carry over between 3-D frames; anything else
    // starts the new dataset at its domain midpoints.
    spec.defaultPositions = m_viewDimension != 3;
    spec.slicePositions = m_slicePosition3d;
    const auto views = currentViews();
    spec.visibleRegions.reserve(views.size());
    spec.outputSizes.reserve(views.size());
    for (const auto* state : views) {
        spec.visibleRegions.push_back(state->visibleRegion);
    }
    return spec;
}

void MainWindow::startPrefetch(int frameIndex)
{
    // Single bounded slot: cancel and drop whatever prefetch came before.
    discardPrefetch();
    auto spec = buildFrameSpec();
    const auto defaultPositions = spec.defaultPositions;
    const auto specGeneration = m_specGeneration;
    const auto generation = m_prefetchGeneration;
    const auto path = m_sequenceFrames[static_cast<std::size_t>(frameIndex)];
    const auto datasetId = DatasetId{
        sequenceDatasetIdBase + ++m_sequenceDatasetCounter};
    m_prefetchStopSource = std::stop_source{};
    const auto cancellation = m_prefetchStopSource.get_token();
    ++m_activeRequests;
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<InitialSliceResult>(this);
    connect(watcher, &QFutureWatcher<InitialSliceResult>::finished, this,
        [this, watcher, generation, frameIndex, specGeneration,
            defaultPositions] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_prefetchGeneration
                    && !m_sequenceFrames.empty()) {
                    m_prefetched = PrefetchedFrame{frameIndex, specGeneration,
                        defaultPositions, std::move(result)};
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception&) {
                // Prefetch failures stay silent: reaching the frame loads it
                // through the normal path and reports any error then.
                if (generation != m_prefetchGeneration) {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [path, datasetId, spec = std::move(spec), cancellation] {
        return executeFrameLoad(path, datasetId, spec, cancellation);
    }));
}

void MainWindow::discardPrefetch()
{
    m_prefetchStopSource.request_stop();
    ++m_prefetchGeneration;
    m_prefetched.reset();
}

void MainWindow::stepSweep(int direction)
{
    if (!m_dataset || m_dataset->metadata().dimension != 3) {
        return;
    }
    const auto axis = m_animationPanel->sweepAxis();
    const auto index = static_cast<std::size_t>(axis);
    const auto& metadata = m_dataset->metadata();
    const auto& domain = metadata.physicalDomain;
    // One finest-level cell per step, wrapping at the domain end.
    const auto cell = metadata.levels.back().cellSize[index];
    auto position = m_slicePosition3d[index] + direction * cell;
    if (position >= domain.upper[index]) {
        position = domain.lower[index] + 0.5 * cell;
    } else if (position < domain.lower[index]) {
        position = domain.upper[index] - 0.5 * cell;
    }
    setSlicePosition(axis, position);
}

void MainWindow::toggleSweepPlayback()
{
    if (m_playbackMode == PlaybackMode::Sweep) {
        setPlaybackMode(PlaybackMode::None);
        return;
    }
    if (!m_dataset || m_dataset->metadata().dimension != 3) {
        return;
    }
    setPlaybackMode(PlaybackMode::Sweep);
}

void MainWindow::toggleSequencePlayback()
{
    if (m_playbackMode == PlaybackMode::Sequence) {
        setPlaybackMode(PlaybackMode::None);
        return;
    }
    if (m_sequenceFrames.size() < 2) {
        return;
    }
    setPlaybackMode(PlaybackMode::Sequence);
}

void MainWindow::setPlaybackMode(PlaybackMode mode)
{
    m_playbackMode = mode;
    m_animationPanel->setSweepPlaying(mode == PlaybackMode::Sweep);
    m_animationPanel->setSequencePlaying(mode == PlaybackMode::Sequence);
    if (mode == PlaybackMode::None) {
        m_playbackTimer->stop();
    } else {
        m_playbackTimer->start(m_animationPanel->frameDelayMs());
    }
}

void MainWindow::playbackTick()
{
    if (m_playbackMode == PlaybackMode::Sweep) {
        if (!m_dataset || m_dataset->metadata().dimension != 3) {
            setPlaybackMode(PlaybackMode::None);
            return;
        }
        // Skip the tick while the previous slice is still on a worker, so a
        // fast Speed setting cannot pile up requests.
        const auto axis = m_animationPanel->sweepAxis();
        if (m_planeViews[static_cast<std::size_t>(axis)].pendingRequests > 0) {
            return;
        }
        stepSweep(1);
        // Bypass the debounce so each tick issues its slice immediately; the
        // in-flight check above is the throttle.
        flushSliceRequests();
        return;
    }
    if (m_playbackMode == PlaybackMode::Sequence) {
        if (m_sequenceFrames.size() < 2) {
            setPlaybackMode(PlaybackMode::None);
            return;
        }
        // Skip the tick while the previous frame is still loading.
        if (m_sequenceInFlight) {
            return;
        }
        goToSequenceFrame(m_sequenceIndex + 1);
    }
}

void MainWindow::applySpeed()
{
    m_playbackTimer->setInterval(m_animationPanel->frameDelayMs());
}

void MainWindow::updateDiagnostics()
{
    auto text = tr("generation: %1\nactive background requests: %2\n"
           "stale results discarded: %3\nmetadata files read: %4\n"
           "metadata bytes read: %5\nblocks read: %6\ncache hits: %7\n"
           "payload bytes read: %8\ncache budget bytes: %9\n"
           "cache resident bytes: %10\ncache pinned bytes: %11\n"
           "cache evictions: %12\nlast frame switch: %13 ms")
            .arg(m_generation)
            .arg(m_activeRequests)
            .arg(m_staleResults)
            .arg(m_lastFilesRead)
            .arg(m_lastBytesRead)
            .arg(m_lastBlocksRead)
            .arg(m_lastCacheHits)
            .arg(m_lastPayloadBytesRead)
            .arg(m_cacheBudgetBytes)
            .arg(m_cacheResidentBytes)
            .arg(m_cachePinnedBytes)
            .arg(m_cacheEvictions)
            .arg(m_lastFrameSwitchMs);
    for (const auto& line : m_probeLines) {
        text += QLatin1Char('\n');
        text += line;
    }
    m_diagnostics->setPlainText(text);
}

} // namespace amrvis::qt
