#pragma once

#include "DatasetWindow.hpp"
#include "NumberFormat.hpp"
#include "SetContoursDialog.hpp"

#include <amrvis/core/Result.hpp>
#include <amrvis/io/PlotfileMetadataReader.hpp>
#include <amrvis/query/SliceQuery.hpp>
#include <amrvis/render2d/Contours.hpp>
#include <amrvis/render2d/ImageBuffer.hpp>
#include <amrvis/render2d/Palette.hpp>
#include <amrvis/render2d/VectorGlyphs.hpp>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QColor;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineF;
class QMenu;
class QPlainTextEdit;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QRectF;
class QWidget;

namespace amrvis {
class PlotfileDataset;
struct DatasetMetadata;
struct LineResult;
enum class CompositionPolicy : std::uint8_t;
}

namespace amrvis::qt {

class AnimationPanel;
class ColorBarWidget;
class DatasetWindow;
class ImageView;
class IsoWidget;
class LinePlotWindow;

enum class RangeMode {
    Visible,
    Level,
    File,
    User
};

struct SliceDisplayResult {
    // The request that produced everything below; PlaneViewState keeps it as
    // the cache key for the re-render-from-cache path (see requestSlice).
    SliceRequest request;
    SliceQueryResult slice;
    ImageBuffer image;
    std::vector<VectorSegment> vectors;
    // Contour modes only: the piecewise plane at data resolution the
    // contours were extracted from (the display plane itself when the data
    // is finer than the display), its bilinear refinement, and the
    // polylines extracted from that refinement, already mapped to
    // display-plane pixel space (empty otherwise).
    ScalarPlane contourPlane;
    ScalarPlane contourFinePlane;
    int contourFineFactor = 1;
    std::vector<ContourPolyline> contourPolylines;
    std::string fieldName;
    double minimum = 0.0;
    double maximum = 1.0;
    bool logarithmic = false;
    DisplayMode mode = DisplayMode::Raster;
    std::uint32_t vectorVField = 0;
    int contourCount = 0;
    // Set when the image was intentionally not re-rendered (contour-only
    // refresh): showSlice keeps the view's current pixmap.
    bool rasterUnchanged = false;
};

struct InitialSliceResult {
    std::shared_ptr<PlotfileDataset> dataset;
    // One entry per displayed view, ordered by normal axis (2-D: one entry).
    std::vector<SliceDisplayResult> displays;
    // First line of the plotfile Header when the path is a plotfile
    // directory; empty for standalone datasets.
    std::string fileVersion;
};

// Everything needed to render one frame's slice(s) off the GUI thread. The
// sequence path builds this from the current UI state so frame switches keep
// the user's field/level/range/log/palette/visible-region settings; empty or
// default entries mean "fall back to the new dataset's defaults" (midpoint
// slice positions, whole domain, 640x640 output).
struct FrameSliceSpec {
    DisplayMode displayMode = DisplayMode::Raster;
    std::uint32_t field = 0;
    int levelSelection = -1;  // level combo data: -1 = finest available
    RangeMode rangeMode = RangeMode::Visible;
    std::optional<std::pair<double, double>> userRange;
    bool logarithmic = false;
    Palette palette;
    std::uint32_t vectorUField = 0;
    std::uint32_t vectorVField = 0;
    int contourCount = 10;
    bool defaultPositions = true;
    std::array<double, 3> slicePositions{0.0, 0.0, 0.0};
    std::vector<std::optional<RealBox>> visibleRegions;  // per view, normal order
    std::vector<std::array<int, 2>> outputSizes;         // per view, normal order
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openDataset(const std::filesystem::path& path, bool metadataOnly = false);
    // Opens a plotfile sequence (the legacy "-a" file animation): frames are
    // the plotfile directories, sorted by name; requires at least two valid
    // plotfiles. Opening a single dataset closes the sequence again.
    void openSequence(const std::vector<std::filesystem::path>& frames);
    // Steps the open sequence by direction frames, wrapping at the ends; the
    // same slot the sequence step buttons and the smoke test hook use.
    void stepSequence(int direction);

signals:
    void datasetOpenFinished(bool success);
    void initialSliceFinished(bool success);
    // Emitted once a sequence frame's slice(s) are on screen; the offscreen
    // smoke test drives frame stepping off it.
    void sequenceFrameDisplayed(int index);
    void sequenceFrameFailed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // Everything that used to be singular about the displayed slice, per
    // view: the 2-D stacked page owns one of these, the 3-D grid owns three
    // (one per plane normal, indexed by normal axis). Each view runs its own
    // async slice pipeline (stop source + generation) so moving one slice
    // plane only re-slices the view normal to it.
    struct PlaneViewState {
        ImageView* view = nullptr;
        int normal = 1;
        QString label;      // "2-D" / "YZ" / "XZ" / "XY"
        ScalarPlane plane;
        // Contour-mode companions of plane: the data-resolution plane the
        // contours were extracted from, its bilinear refinement, and the
        // display-space polylines. Cleared and updated exactly where plane
        // is; together with the cache key below they let range, palette,
        // and contour-count changes refresh without a new SliceQuery.
        ScalarPlane contourPlane;
        ScalarPlane contourFinePlane;
        int contourFineFactor = 1;
        std::vector<ContourPolyline> contourPolylines;
        QString fieldName;
        std::optional<RealBox> visibleRegion;
        double displayMinimum = 0.0;
        double displayMaximum = 1.0;
        bool displayLogarithmic = false;
        std::vector<VectorSegment> vectorSegments;
        // Cache key of the slice that produced the planes above: a UI change
        // that leaves every key field untouched (palette/log/range/contour
        // count) is satisfied from the cached planes instead of querying
        // again (see requestSlice).
        SliceRequest cachedRequest{};
        bool hasCachedRequest = false;
        DisplayMode cachedMode = DisplayMode::Raster;
        std::uint32_t cachedVectorVField = 0;
        int cachedContourCount = 0;
        std::stop_source stopSource;
        std::uint64_t sliceGeneration = 0;
        // Slice requests currently on a worker for this view; the sweep
        // playback skips ticks while one is in flight.
        int pendingRequests = 0;
    };

    // One prefetched sequence frame: the dataset plus its rendered slice(s),
    // consumable only while the slice spec that produced it is unchanged.
    struct PrefetchedFrame {
        int frameIndex = -1;
        std::uint64_t specGeneration = 0;
        bool defaultPositions = false;
        InitialSliceResult result;
    };

    void chooseDataset(bool newWindow = false);
    void chooseStandaloneDataset();
    // A fresh independent top-level window (WA_DeleteOnClose) for "open in new
    // window"; it shares no view/cache state with this one.
    [[nodiscard]] MainWindow* createNewWindow();
    void exportImage();
    void exportSliceData();
    void createMenus();
    void rebuildLevelMenu();
    void syncMenuChecks();
    void syncPaletteChecks();
    void selectBuiltinPalette(int index);
    void loadPaletteFile();
    void applyPalette(const Palette& palette, std::optional<int> builtinIndex,
        const QString& filePath);
    // Per-field user range: each field remembers its own RangeMode and, when
    // User, its min/max. commitFieldRange snapshots the current widgets for a
    // field, applyFieldRange loads a field's snapshot (or the Visible default)
    // back into the widgets, and resetRangeState clears everything for a fresh
    // dataset.
    void commitFieldRange(std::uint32_t field);
    void applyFieldRange(std::uint32_t field);
    void resetRangeState();
    void showContoursDialog();
    void applyContourSettings(DisplayMode mode, int count, int uField, int vField);
    void showNumberFormatDialog();
    void applyNumberFormat(const QString& format);
    void validateVectorMode();
    void ensureVectorFieldDefaults();
    void showDatasetWindow();
    void closeDatasetWindow();
    void refreshDatasetWindow();
    void datasetCellActivated(const RealBox& physicalCell);
    [[nodiscard]] std::optional<DatasetRequest> buildDatasetRequest() const;
    void showKeyboardMouseReference();
    void showAboutDialog();
    void showMetadata(const PlotfileMetadataResult& result, const std::filesystem::path& path);
    void updateDiagnostics();
    void updateAnimationDockVisibility();
    void updateWindowTitle();
    void restoreSettings();
    void saveSettings();

    // Per-view wiring and display updates.
    void wireView(PlaneViewState& state);
    [[nodiscard]] std::vector<PlaneViewState*> currentViews();
    void setActiveView(PlaneViewState& state);
    [[nodiscard]] std::array<int, 2> displayAxes(int normal) const;
    void probeMoved(PlaneViewState& state, int x, int displayY);
    void probeClicked(PlaneViewState& state, int x, int displayY);
    [[nodiscard]] QString probeReadout(
        const PlaneViewState& state, int x, int displayY) const;
    void rubberBandZoom(PlaneViewState& state, const QRectF& sceneRect);
    void linePlotRequested(PlaneViewState& state, int imageX, int imageY,
        Qt::MouseButton button);
    void sliceMoveRequested(PlaneViewState& state, int imageX, int imageY,
        Qt::MouseButton button);
    void fitView(PlaneViewState& state);
    void fitViewToWindow();
    void showSlice(PlaneViewState& state, const SliceDisplayResult& display);
    void updateOverlay(PlaneViewState& state);
    void updateOverlays();
    void updateGridBoxes(PlaneViewState& state);
    void updateGridBoxes();
    void updateCrosshairs(PlaneViewState& state);
    void updateCrosshairs();
    [[nodiscard]] QLineF planeSegmentToScene(const PlaneViewState& state,
        float x0, float y0, float x1, float y1) const;
    [[nodiscard]] QColor contourValueColor(const PlaneViewState& state,
        double value) const;
    [[nodiscard]] QColor monochromeContourColor() const;
    [[nodiscard]] QColor sliceAxisColor(int axis) const;

    // Shared 3-D slice positions (physical coordinates per axis).
    void configureSlicePositionControls();
    void setSlicePosition(int axis, double value);

    // Slice requests: the debounce timer coalesces into per-view requests.
    // rasterDirty false means the trigger (contour mode/count) cannot change
    // the raster, so a cache-satisfied request skips the image re-render.
    void scheduleSliceRequest(bool rasterDirty = true);
    void scheduleSliceRequest(PlaneViewState& state, bool rasterDirty = true);
    void flushSliceRequests();
    void requestSlice(PlaneViewState& state, bool rasterDirty);
    void requestInitialSlice(const std::filesystem::path& path, std::uint64_t generation);
    void configureSliceControls();
    void appendLinePlotCurve(const LineResult& line, const std::string& fieldName,
        int dimension, int primaryFixedAxis,
        const std::array<double, 3>& fixedCoordinates, int maximumLevel,
        CompositionPolicy composition);

    // Animation: one shared playback timer drives either the 3-D plane sweep
    // or plotfile-sequence playback, never both at once.
    enum class PlaybackMode {
        None,
        Sweep,
        Sequence
    };
    void choosePlotfileSequence(bool newWindow = false);
    void closeSequence();
    void goToSequenceFrame(int index);
    void toggleSequencePlayback();
    void stepSweep(int direction);
    void toggleSweepPlayback();
    void setPlaybackMode(PlaybackMode mode);
    void playbackTick();
    void applySpeed();

    // Sequence frame switching. At most one sync frame load and one
    // prefetch are alive; everything superseded is cancelled via stop
    // sources and discarded via generation counters.
    void startFrameLoad(int index, std::uint64_t generation);
    void finishFrameLoad(InitialSliceResult result, bool defaultPositions);
    void displayFrameResult(InitialSliceResult& result, bool defaultPositions);
    void configureSequenceControls(bool defaultPositions);
    [[nodiscard]] FrameSliceSpec buildFrameSpec();
    void startPrefetch(int frameIndex);
    void discardPrefetch();

    QStackedWidget* m_stack = nullptr;
    IsoWidget* m_isoWidget = nullptr;
    QLabel* m_probeLabel = nullptr;
    ColorBarWidget* m_colorBar = nullptr;
    LinePlotWindow* m_linePlotWindow = nullptr;
    // Cancels in-flight line-plot queries on dataset switch or window close so
    // a late result neither reopens a closed window nor wastes I/O.
    std::stop_source m_linePlotStopSource;
    DatasetWindow* m_datasetWindow = nullptr;
    QComboBox* m_fieldSelector = nullptr;
    QComboBox* m_levelSelector = nullptr;
    QComboBox* m_rangeMode = nullptr;
    QCheckBox* m_logarithmic = nullptr;
    QCheckBox* m_gridBoxes = nullptr;
    QDoubleSpinBox* m_rangeMinimum = nullptr;
    QDoubleSpinBox* m_rangeMaximum = nullptr;
    // Per-field range state for the current dataset. m_trackedField is the
    // field the range widgets currently represent; the field selector swaps
    // snapshots through this map when the user changes fields.
    struct FieldRange {
        RangeMode mode = RangeMode::Visible;
        std::optional<std::pair<double, double>> userRange;
    };
    std::unordered_map<std::uint32_t, FieldRange> m_fieldRanges;
    std::uint32_t m_trackedField = 0;
    QWidget* m_slicePositionControls = nullptr;
    std::array<QDoubleSpinBox*, 3> m_sliceSpinboxes{nullptr, nullptr, nullptr};
    QTimer* m_sliceDebounce = nullptr;
    QTreeWidget* m_metadataTree = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    QDockWidget* m_metadataDock = nullptr;
    QDockWidget* m_diagnosticsDock = nullptr;
    QDockWidget* m_colorBarDock = nullptr;
    QDockWidget* m_animationDock = nullptr;
    QMenu* m_levelMenu = nullptr;
    QActionGroup* m_levelGroup = nullptr;
    QActionGroup* m_paletteGroup = nullptr;
    QAction* m_boxesAction = nullptr;
    QAction* m_fitScaleAction = nullptr;
    QAction* m_contoursAction = nullptr;
    QAction* m_datasetAction = nullptr;
    std::shared_ptr<PlotfileDataset> m_dataset;
    std::shared_ptr<const DatasetMetadata> m_openMetadata;
    std::string m_fileVersion;
    PlaneViewState m_view2d;
    std::array<PlaneViewState, 3> m_planeViews;
    PlaneViewState* m_activeView = nullptr;
    int m_viewDimension = 0;
    std::array<double, 3> m_slicePosition3d{0.0, 0.0, 0.0};
    bool m_pendingAllViews = false;
    std::vector<PlaneViewState*> m_pendingViews;
    // OR of the rasterDirty flags of the coalesced pending requests.
    bool m_pendingRasterDirty = false;
    std::stop_source m_initialStopSource;
    DisplayMode m_displayMode = DisplayMode::Raster;
    int m_contourCount = 10;
    int m_vectorUField = -1;
    int m_vectorVField = -1;
    std::filesystem::path m_datasetPath;
    Palette m_palette = builtinPalette(BuiltinPalette::Rainbow);
    int m_builtinIndex = 0;
    bool m_paletteFromFile = false;
    QString m_paletteFilePath;
    QString m_numberFormat = defaultNumberFormat();
    QStringList m_probeLines;
    bool m_controlsReady = false;
    std::uint64_t m_generation = 0;
    std::uint64_t m_activeRequests = 0;
    std::uint64_t m_staleResults = 0;
    std::uint64_t m_lastFilesRead = 0;
    std::uint64_t m_lastBytesRead = 0;
    std::uint64_t m_lastBlocksRead = 0;
    std::uint64_t m_lastCacheHits = 0;
    std::uint64_t m_lastPayloadBytesRead = 0;
    std::uint64_t m_cacheBudgetBytes = 0;
    std::uint64_t m_cacheResidentBytes = 0;
    std::uint64_t m_cachePinnedBytes = 0;
    std::uint64_t m_cacheEvictions = 0;

    // Animation state.
    AnimationPanel* m_animationPanel = nullptr;
    QTimer* m_playbackTimer = nullptr;
    PlaybackMode m_playbackMode = PlaybackMode::None;
    std::vector<std::filesystem::path> m_sequenceFrames;
    int m_sequenceIndex = -1;
    bool m_sequenceInFlight = false;
    // Bumped by every slice-affecting UI change; prefetched frames store the
    // value they were built against and are discarded once it moves on.
    std::uint64_t m_specGeneration = 0;
    // Bumped whenever the prefetch slot is cancelled/invalidated, so a late
    // prefetch watcher knows to drop its result.
    std::uint64_t m_prefetchGeneration = 0;
    std::uint64_t m_sequenceDatasetCounter = 0;
    std::stop_source m_prefetchStopSource;
    std::optional<PrefetchedFrame> m_prefetched;
    QElapsedTimer m_frameTimer;
    qint64 m_lastFrameSwitchMs = 0;
};

} // namespace amrvis::qt
