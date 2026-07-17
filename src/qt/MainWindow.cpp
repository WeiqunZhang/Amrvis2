#include "MainWindow.hpp"
#include "ColorBarWidget.hpp"
#include "ImageView.hpp"

#include <amrvis/io/PlotfileDataset.hpp>
#include <amrvis/io/StandaloneMetadataReader.hpp>
#include <amrvis/core/Statistics.hpp>
#include <amrvis/query/SliceQuery.hpp>
#include <amrvis/render2d/ScalarRenderer.hpp>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <utility>

namespace amrvis::qt {
namespace {

constexpr std::uint64_t initialCacheBudget = 256ULL * 1024ULL * 1024ULL;

enum class RangeMode {
    Visible,
    Level,
    File,
    User
};

struct SliceDisplayResult {
    SliceQueryResult slice;
    ImageBuffer image;
    std::string fieldName;
    double minimum = 0.0;
    double maximum = 1.0;
    bool logarithmic = false;
};

struct InitialSliceResult {
    std::shared_ptr<PlotfileDataset> dataset;
    SliceDisplayResult display;
};

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

SliceDisplayResult executeSlice(const std::shared_ptr<PlotfileDataset>& dataset,
    const SliceRequest& request,
    RangeMode rangeMode,
    const std::optional<std::pair<double, double>>& userRange,
    bool logarithmic, std::stop_token cancellation)
{
    SliceDisplayResult result;
    result.slice = SliceQuery(*dataset).execute(request, cancellation);
    auto selectedRange = userRange;
    if (rangeMode == RangeMode::Level || rangeMode == RangeMode::File) {
        const auto statistics = metadataValueRange(dataset->metadata(), request.field,
            rangeMode == RangeMode::Level
                ? std::optional<int>(request.maximumLevel) : std::nullopt);
        if (!statistics) {
            throw std::runtime_error(
                "the selected dataset does not provide this metadata range");
        }
        selectedRange = std::pair{statistics->minimum, statistics->maximum};
    }
    auto [minimum, maximum] = selectedRange
        ? *selectedRange : finiteRange(result.slice.plane, logarithmic);
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
    result.minimum = minimum;
    result.maximum = maximum;
    result.logarithmic = logarithmic;
    result.fieldName = dataset->metadata().fields[request.field.value].name;
    result.image = renderScalarPlane(result.slice.plane,
        ScalarRenderSettings{
            .minimum = minimum,
            .maximum = maximum,
            .logarithmic = logarithmic
        });
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Amrvis2"));
    resize(960, 720);

    m_imageView = new ImageView(this);
    m_imageView->setMinimumSize(320, 240);
    m_imageView->setPlaceholder(tr("Open an AMReX dataset to display a slice"));
    setCentralWidget(m_imageView);

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
    sliceToolbar->addWidget(new QLabel(tr("Normal:"), sliceToolbar));
    m_normalSelector = new QComboBox(sliceToolbar);
    sliceToolbar->addWidget(m_normalSelector);
    sliceToolbar->addWidget(new QLabel(tr("Position:"), sliceToolbar));
    m_slicePosition = new QDoubleSpinBox(sliceToolbar);
    m_slicePosition->setDecimals(8);
    m_slicePosition->setMinimumWidth(120);
    sliceToolbar->addWidget(m_slicePosition);
    m_sliceSlider = new QSlider(Qt::Horizontal, sliceToolbar);
    m_sliceSlider->setRange(0, 1000);
    m_sliceSlider->setMinimumWidth(160);
    sliceToolbar->addWidget(m_sliceSlider);
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
    connect(m_sliceDebounce, &QTimer::timeout, this, [this] { requestSlice(); });
    connect(m_fieldSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { scheduleSliceRequest(); });
    connect(m_levelSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { scheduleSliceRequest(); });
    connect(m_normalSelector, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) {
            updateSlicePositionRange();
            scheduleSliceRequest();
        });
    connect(m_slicePosition, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this](double value) {
            if (!m_controlsReady || !m_dataset || m_dataset->metadata().dimension != 3) {
                return;
            }
            const auto axis = static_cast<std::size_t>(m_normalSelector->currentData().toInt());
            const auto& domain = m_dataset->metadata().physicalDomain;
            const auto extent = domain.upper[axis] - domain.lower[axis];
            const auto normalized = extent > 0.0
                ? (value - domain.lower[axis]) / extent : 0.0;
            const QSignalBlocker blocker(m_sliceSlider);
            m_sliceSlider->setValue(static_cast<int>(
                std::lround(std::clamp(normalized, 0.0, 1.0) * 1000.0)));
            scheduleSliceRequest();
        });
    connect(m_sliceSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_controlsReady || !m_dataset || m_dataset->metadata().dimension != 3) {
            return;
        }
        const auto axis = static_cast<std::size_t>(m_normalSelector->currentData().toInt());
        const auto& domain = m_dataset->metadata().physicalDomain;
        const auto position = domain.lower[axis]
            + static_cast<double>(value) / 1000.0
                * (domain.upper[axis] - domain.lower[axis]);
        const QSignalBlocker blocker(m_slicePosition);
        m_slicePosition->setValue(position);
        scheduleSliceRequest();
    });
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
    m_normalSelector->setEnabled(false);
    m_slicePosition->setEnabled(false);
    m_sliceSlider->setEnabled(false);
    m_rangeMode->setEnabled(false);
    m_logarithmic->setEnabled(false);
    m_gridBoxes->setEnabled(false);

    auto* metadataDock = new QDockWidget(tr("Dataset Metadata"), this);
    m_metadataTree = new QTreeWidget(metadataDock);
    m_metadataTree->setColumnCount(2);
    m_metadataTree->setHeaderLabels({tr("Property"), tr("Value")});
    m_metadataTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    metadataDock->setWidget(m_metadataTree);
    addDockWidget(Qt::LeftDockWidgetArea, metadataDock);

    auto* diagnosticsDock = new QDockWidget(tr("Diagnostics"), this);
    m_diagnostics = new QPlainTextEdit(diagnosticsDock);
    m_diagnostics->setReadOnly(true);
    diagnosticsDock->setWidget(m_diagnostics);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock);

    auto* colorBarDock = new QDockWidget(tr("Color Scale"), this);
    m_colorBar = new ColorBarWidget(colorBarDock);
    colorBarDock->setWidget(m_colorBar);
    addDockWidget(Qt::RightDockWidgetArea, colorBarDock);

    auto* openAction = new QAction(tr("&Open Plotfile Directory..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] { chooseDataset(); });

    auto* openStandaloneAction = new QAction(tr("Open &Standalone FAB/MultiFab..."), this);
    connect(openStandaloneAction, &QAction::triggered,
        this, [this] { chooseStandaloneDataset(); });

    auto* exportAction = new QAction(tr("&Export Image..."), this);
    connect(exportAction, &QAction::triggered, this, [this] { exportImage(); });

    auto* exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(openStandaloneAction);
    fileMenu->addAction(exportAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    m_probeLabel = new QLabel(statusBar());
    statusBar()->addPermanentWidget(m_probeLabel);
    connect(m_imageView, &ImageView::probeMoved, this, [this](int x, int displayY) {
        if (m_displayPlane.width <= 0 || m_displayPlane.height <= 0) {
            return;
        }
        const auto y = m_displayPlane.height - 1 - displayY;
        const auto offset = static_cast<std::size_t>(x)
            + static_cast<std::size_t>(m_displayPlane.width) * static_cast<std::size_t>(y);
        if (offset >= m_displayPlane.values.size() || m_displayPlane.valid[offset] == 0) {
            m_probeLabel->setText(tr("no data"));
            return;
        }
        std::array<int, 2> axes{0, 1};
        if (m_displayDimension == 3) {
            std::size_t next = 0;
            for (int axis = 0; axis < 3; ++axis) {
                if (axis != m_displayNormalDirection) {
                    axes[next++] = axis;
                }
            }
        }
        const auto xAxis = static_cast<std::size_t>(axes[0]);
        const auto yAxis = static_cast<std::size_t>(axes[1]);
        const auto physicalX = m_displayPlane.physicalRegion.lower[xAxis]
            + (static_cast<double>(x) + 0.5)
                / static_cast<double>(m_displayPlane.width)
                * (m_displayPlane.physicalRegion.upper[xAxis]
                    - m_displayPlane.physicalRegion.lower[xAxis]);
        const auto physicalY = m_displayPlane.physicalRegion.lower[yAxis]
            + (static_cast<double>(y) + 0.5)
                / static_cast<double>(m_displayPlane.height)
                * (m_displayPlane.physicalRegion.upper[yAxis]
                    - m_displayPlane.physicalRegion.lower[yAxis]);
        constexpr std::array<const char*, 3> axisNames{"x", "y", "z"};
        m_probeLabel->setText(tr("%1=%2 %3=%4 value=%5 level=%6")
            .arg(QString::fromLatin1(axisNames[xAxis]))
            .arg(physicalX, 0, 'g', 7)
            .arg(QString::fromLatin1(axisNames[yAxis]))
            .arg(physicalY, 0, 'g', 7)
            .arg(m_displayPlane.values[offset], 0, 'g', 8)
            .arg(m_displayPlane.sourceLevel[offset]));
    });
    statusBar()->showMessage(tr("No dataset open"));
    updateDiagnostics();
}

void MainWindow::chooseDataset()
{
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Open AMReX plotfile"));
    if (!directory.isEmpty()) {
        openDataset(directory.toStdString());
    }
}

void MainWindow::chooseStandaloneDataset()
{
    const auto filename = QFileDialog::getOpenFileName(this,
        tr("Open standalone AMReX FAB or MultiFab header"), QString(),
        tr("AMReX data (*)"));
    if (!filename.isEmpty()) {
        openDataset(filename.toStdString());
    }
}

void MainWindow::exportImage()
{
    if (!m_imageView->hasImage()) {
        QMessageBox::information(this, tr("No image"),
            tr("Open a dataset before exporting an image."));
        return;
    }
    const auto filename = QFileDialog::getSaveFileName(
        this, tr("Export scalar image"), QString(), tr("PNG image (*.png)"));
    if (!filename.isEmpty() && !m_imageView->image().save(filename, "PNG")) {
        QMessageBox::critical(this, tr("Cannot export image"),
            tr("The image could not be written to %1.").arg(filename));
    }
}

void MainWindow::openDataset(const std::filesystem::path& path, bool metadataOnly)
{
    m_sliceStopSource.request_stop();
    ++m_sliceGeneration;
    m_sliceDebounce->stop();
    m_controlsReady = false;
    m_dataset.reset();
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
    m_normalSelector->setEnabled(false);
    m_slicePosition->setEnabled(false);
    m_sliceSlider->setEnabled(false);
    m_rangeMode->setEnabled(false);
    m_logarithmic->setEnabled(false);
    m_gridBoxes->setEnabled(false);
    m_rangeMinimum->setEnabled(false);
    m_rangeMaximum->setEnabled(false);
    {
        const QSignalBlocker rangeModeBlocker(m_rangeMode);
        const QSignalBlocker logarithmicBlocker(m_logarithmic);
        m_rangeMode->setCurrentIndex(0);
        m_logarithmic->setChecked(false);
        m_gridBoxes->setChecked(false);
    }
    m_imageView->setPlaceholder(tr("Loading dataset..."));
    m_displayPlane = {};
    m_displayDimension = 0;
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
                        QString::fromUtf8(error.what()));
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
    m_sliceStopSource.request_stop();
    m_sliceStopSource = std::stop_source{};
    const auto cancellation = m_sliceStopSource.get_token();
    const auto sliceGeneration = ++m_sliceGeneration;
    ++m_activeRequests;
    statusBar()->showMessage(tr("Loading initial slice..."));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<InitialSliceResult>(this);
    connect(watcher, &QFutureWatcher<InitialSliceResult>::finished, this,
        [this, watcher, generation, sliceGeneration, cancellation, path] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation && sliceGeneration == m_sliceGeneration) {
                    m_dataset = result.dataset;
                    configureSliceControls();
                    showSlice(result.display.image, result.display.slice, path,
                        QString::fromStdString(result.display.fieldName),
                        result.display.minimum, result.display.maximum,
                        result.display.logarithmic);
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
                if (generation == m_generation && sliceGeneration == m_sliceGeneration
                    && !cancellation.stop_requested()) {
                    statusBar()->showMessage(tr("Initial slice failed"));
                    QMessageBox::critical(this, tr("Cannot load slice"),
                        QString::fromUtf8(error.what()));
                    emit initialSliceFinished(false);
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([path, generation, cancellation] {
        InitialSliceResult result;
        result.dataset = std::make_shared<PlotfileDataset>(
            path, DatasetId{generation}, initialCacheBudget);
        const auto& metadata = result.dataset->metadata();
        if (metadata.fields.empty()) {
            throw std::runtime_error("dataset has no scalar fields to display");
        }

        SliceRequest request;
        request.dataset = result.dataset->id();
        request.field = FieldId{0};
        request.normalDirection = metadata.dimension == 3 ? 2 : 1;
        request.visibleRegion = metadata.physicalDomain;
        request.maximumLevel = metadata.finestLevel;
        request.outputSize = {640, 640};
        if (metadata.dimension == 3) {
            const auto lower = metadata.physicalDomain.lower[2];
            const auto upper = metadata.physicalDomain.upper[2];
            request.physicalPosition = lower + 0.5 * (upper - lower);
        }

        result.display = executeSlice(result.dataset, request, RangeMode::Visible,
            std::nullopt, false, cancellation);
        return result;
    }));
}

void MainWindow::configureSliceControls()
{
    if (!m_dataset) {
        return;
    }
    const QSignalBlocker fieldBlocker(m_fieldSelector);
    const QSignalBlocker levelBlocker(m_levelSelector);
    const QSignalBlocker normalBlocker(m_normalSelector);
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

    m_normalSelector->clear();
    if (metadata.dimension == 3) {
        m_normalSelector->addItem(tr("X"), 0);
        m_normalSelector->addItem(tr("Y"), 1);
        m_normalSelector->addItem(tr("Z"), 2);
        m_normalSelector->setCurrentIndex(2);
    } else {
        m_normalSelector->addItem(tr("2-D"), 1);
        m_normalSelector->setCurrentIndex(0);
    }

    updateSlicePositionRange();
    m_controlsReady = true;
    m_fieldSelector->setEnabled(true);
    m_levelSelector->setEnabled(true);
    const auto isThreeDimensional = metadata.dimension == 3;
    m_normalSelector->setEnabled(isThreeDimensional);
    m_slicePosition->setEnabled(isThreeDimensional);
    m_sliceSlider->setEnabled(isThreeDimensional);
    m_rangeMode->setEnabled(true);
    m_logarithmic->setEnabled(true);
    m_gridBoxes->setEnabled(true);
}

void MainWindow::updateSlicePositionRange()
{
    if (!m_dataset || m_normalSelector->currentIndex() < 0) {
        return;
    }
    const auto& metadata = m_dataset->metadata();
    const auto axis = static_cast<std::size_t>(m_normalSelector->currentData().toInt());
    const auto lower = metadata.physicalDomain.lower[axis];
    const auto upper = metadata.physicalDomain.upper[axis];
    const auto lastInterior = std::nextafter(upper, lower);
    const auto midpoint = lower + 0.5 * (upper - lower);
    const QSignalBlocker positionBlocker(m_slicePosition);
    const QSignalBlocker sliderBlocker(m_sliceSlider);
    m_slicePosition->setRange(lower, lastInterior);
    m_slicePosition->setSingleStep(
        metadata.levels.back().cellSize[axis]);
    m_slicePosition->setValue(midpoint);
    m_sliceSlider->setValue(500);
}

void MainWindow::scheduleSliceRequest()
{
    if (m_controlsReady && m_dataset) {
        m_sliceDebounce->start();
    }
}

void MainWindow::requestSlice()
{
    if (!m_controlsReady || !m_dataset
        || m_fieldSelector->currentIndex() < 0
        || m_levelSelector->currentIndex() < 0
        || m_normalSelector->currentIndex() < 0) {
        return;
    }

    const auto dataset = m_dataset;
    SliceRequest request;
    request.dataset = dataset->id();
    request.field.value = m_fieldSelector->currentData().toUInt();
    request.normalDirection = m_normalSelector->currentData().toInt();
    request.physicalPosition = m_slicePosition->value();
    request.visibleRegion = dataset->metadata().physicalDomain;
    const auto level = m_levelSelector->currentData().toInt();
    request.composition = level < 0
        ? CompositionPolicy::FinestAvailable : CompositionPolicy::ExactLevel;
    request.maximumLevel = level < 0 ? dataset->metadata().finestLevel : level;
    request.outputSize = {640, 640};

    const auto rangeMode = static_cast<RangeMode>(m_rangeMode->currentData().toInt());
    std::optional<std::pair<double, double>> userRange;
    if (rangeMode == RangeMode::User) {
        userRange = std::pair{m_rangeMinimum->value(), m_rangeMaximum->value()};
    }
    const auto logarithmic = m_logarithmic->isChecked();

    m_sliceStopSource.request_stop();
    m_sliceStopSource = std::stop_source{};
    const auto cancellation = m_sliceStopSource.get_token();
    const auto generation = m_generation;
    const auto sliceGeneration = ++m_sliceGeneration;
    const auto path = m_datasetPath;
    ++m_activeRequests;
    statusBar()->showMessage(tr("Loading %1...").arg(
        m_fieldSelector->currentText()));
    updateDiagnostics();

    auto* watcher = new QFutureWatcher<SliceDisplayResult>(this);
    connect(watcher, &QFutureWatcher<SliceDisplayResult>::finished, this,
        [this, watcher, dataset, generation, sliceGeneration, cancellation, path] {
            --m_activeRequests;
            try {
                auto result = watcher->result();
                if (generation == m_generation && sliceGeneration == m_sliceGeneration) {
                    showSlice(result.image, result.slice, path,
                        QString::fromStdString(result.fieldName),
                        result.minimum, result.maximum, result.logarithmic);
                    const auto cache = dataset->cacheMetrics();
                    m_cacheBudgetBytes = cache.budgetBytes;
                    m_cacheResidentBytes = cache.residentBytes;
                    m_cachePinnedBytes = cache.pinnedBytes;
                    m_cacheEvictions = cache.evictions;
                } else {
                    ++m_staleResults;
                }
            } catch (const std::exception& error) {
                if (generation == m_generation && sliceGeneration == m_sliceGeneration
                    && !cancellation.stop_requested()) {
                    statusBar()->showMessage(tr("Slice request failed"));
                    QMessageBox::critical(this, tr("Cannot load slice"),
                        QString::fromUtf8(error.what()));
                } else {
                    ++m_staleResults;
                }
            }
            updateDiagnostics();
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run(
        [dataset, request, rangeMode, userRange, logarithmic, cancellation] {
        return executeSlice(dataset, request, rangeMode,
            userRange, logarithmic, cancellation);
    }));
}

void MainWindow::updateGridBoxes()
{
    std::vector<GridBoxOverlay> overlays;
    if (!m_gridBoxes->isChecked() || !m_dataset || !m_imageView->hasImage()
        || m_displayPlane.width <= 0 || m_displayPlane.height <= 0) {
        m_imageView->setGridBoxes(overlays);
        return;
    }

    const auto& metadata = m_dataset->metadata();
    const auto normal = metadata.dimension == 3 ? m_displayNormalDirection : -1;
    std::array<int, 2> axes{0, 1};
    if (metadata.dimension == 3) {
        std::size_t next = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (axis != normal) {
                axes[next++] = axis;
            }
        }
    }
    const auto selectedLevel = m_levelSelector->currentData().toInt();
    const auto firstLevel = selectedLevel < 0 ? 0 : selectedLevel;
    const auto lastLevel = selectedLevel < 0
        ? metadata.finestLevel : selectedLevel;
    constexpr std::array<QRgb, 4> colors{
        qRgb(255, 255, 255), qRgb(0, 255, 255),
        qRgb(255, 210, 0), qRgb(255, 80, 200)};

    const auto xAxis = static_cast<std::size_t>(axes[0]);
    const auto yAxis = static_cast<std::size_t>(axes[1]);
    const auto xExtent = m_displayPlane.physicalRegion.upper[xAxis]
        - m_displayPlane.physicalRegion.lower[xAxis];
    const auto yExtent = m_displayPlane.physicalRegion.upper[yAxis]
        - m_displayPlane.physicalRegion.lower[yAxis];
    for (int levelIndex = firstLevel; levelIndex <= lastLevel; ++levelIndex) {
        const auto& level = metadata.levels[static_cast<std::size_t>(levelIndex)];
        for (const auto& box : level.boxes) {
            if (normal >= 0) {
                const auto direction = static_cast<std::size_t>(normal);
                const auto normalLower = metadata.physicalDomain.lower[direction]
                    + static_cast<double>(static_cast<std::int64_t>(box.lower[direction])
                        - level.domain.lower[direction]) * level.cellSize[direction];
                const auto normalUpper = metadata.physicalDomain.lower[direction]
                    + static_cast<double>(static_cast<std::int64_t>(box.upper[direction])
                        - level.domain.lower[direction] + 1) * level.cellSize[direction];
                if (m_slicePosition->value() < normalLower
                    || m_slicePosition->value() >= normalUpper) {
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
            const auto pixelX0 = (xLower - m_displayPlane.physicalRegion.lower[xAxis])
                / xExtent * m_displayPlane.width;
            const auto pixelX1 = (xUpper - m_displayPlane.physicalRegion.lower[xAxis])
                / xExtent * m_displayPlane.width;
            const auto pixelY0 = m_displayPlane.height
                - (yUpper - m_displayPlane.physicalRegion.lower[yAxis])
                    / yExtent * m_displayPlane.height;
            const auto pixelY1 = m_displayPlane.height
                - (yLower - m_displayPlane.physicalRegion.lower[yAxis])
                    / yExtent * m_displayPlane.height;
            QRectF rectangle(QPointF(pixelX0, pixelY0), QPointF(pixelX1, pixelY1));
            rectangle = rectangle.normalized().intersected(
                QRectF(0.0, 0.0, m_displayPlane.width, m_displayPlane.height));
            if (!rectangle.isEmpty()) {
                overlays.push_back({rectangle,
                    QColor::fromRgb(colors[static_cast<std::size_t>(levelIndex)
                        % colors.size()])});
            }
        }
    }
    m_imageView->setGridBoxes(overlays);
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

    m_lastFilesRead = result.metrics.filesRead;
    m_lastBytesRead = result.metrics.bytesRead;
    statusBar()->showMessage(tr("Metadata loaded: %1 field(s), %2 level(s)")
        .arg(metadata.fields.size()).arg(metadata.levels.size()));
}

void MainWindow::showSlice(const ImageBuffer& image, const SliceQueryResult& result,
    const std::filesystem::path& path, const QString& fieldName,
    double minimum, double maximum, bool logarithmic)
{
    if (!image.valid()) {
        throw std::runtime_error("renderer produced an invalid image");
    }
    const QImage wrapped(reinterpret_cast<const uchar*>(image.rgba.data()),
        image.width, image.height, image.strideBytes, QImage::Format_ARGB32);
    const auto displayImage = wrapped.mirrored(false, true).copy();
    m_imageView->setImage(displayImage);
    m_displayPlane = result.plane;
    if (m_dataset) {
        m_displayDimension = m_dataset->metadata().dimension;
        m_displayNormalDirection = m_normalSelector->currentData().toInt();
    }
    m_imageView->setToolTip(tr("%1\nfield: %2\nrange: [%3, %4]%5")
        .arg(QString::fromStdString(path.string()))
        .arg(fieldName)
        .arg(minimum, 0, 'g', 8)
        .arg(maximum, 0, 'g', 8)
        .arg(logarithmic ? tr(" (log)") : QString()));
    m_colorBar->setFieldRange(
        logarithmic ? fieldName + tr(" (log)") : fieldName, minimum, maximum);
    if (static_cast<RangeMode>(m_rangeMode->currentData().toInt())
        != RangeMode::User) {
        const QSignalBlocker minimumBlocker(m_rangeMinimum);
        const QSignalBlocker maximumBlocker(m_rangeMaximum);
        m_rangeMinimum->setValue(minimum);
        m_rangeMaximum->setValue(maximum);
    }
    updateGridBoxes();

    m_lastBlocksRead = result.metrics.blocksRead;
    m_lastCacheHits = result.metrics.cacheHits;
    m_lastPayloadBytesRead = result.metrics.payloadBytesRead;
    statusBar()->showMessage(tr("Displayed %1 at finest level; range [%2, %3]")
        .arg(fieldName)
        .arg(minimum, 0, 'g', 8)
        .arg(maximum, 0, 'g', 8));
}

void MainWindow::updateDiagnostics()
{
    m_diagnostics->setPlainText(
        tr("generation: %1\nactive background requests: %2\n"
           "stale results discarded: %3\nmetadata files read: %4\n"
           "metadata bytes read: %5\nblocks read: %6\ncache hits: %7\n"
           "payload bytes read: %8\ncache budget bytes: %9\n"
           "cache resident bytes: %10\ncache pinned bytes: %11\n"
           "cache evictions: %12")
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
            .arg(m_cacheEvictions));
}

} // namespace amrvis::qt
