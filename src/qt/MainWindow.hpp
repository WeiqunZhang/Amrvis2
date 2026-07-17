#pragma once

#include <amrvis/core/Result.hpp>
#include <amrvis/io/PlotfileMetadataReader.hpp>
#include <amrvis/render2d/ImageBuffer.hpp>

#include <QMainWindow>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QTimer;
class QTreeWidget;

namespace amrvis {
class PlotfileDataset;
struct SliceQueryResult;
}

namespace amrvis::qt {

class ColorBarWidget;
class ImageView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openDataset(const std::filesystem::path& path, bool metadataOnly = false);

signals:
    void datasetOpenFinished(bool success);
    void initialSliceFinished(bool success);

private:
    void chooseDataset();
    void chooseStandaloneDataset();
    void exportImage();
    void requestInitialSlice(const std::filesystem::path& path, std::uint64_t generation);
    void configureSliceControls();
    void updateSlicePositionRange();
    void scheduleSliceRequest();
    void requestSlice();
    void updateGridBoxes();
    void showMetadata(const PlotfileMetadataResult& result, const std::filesystem::path& path);
    void showSlice(const ImageBuffer& image, const SliceQueryResult& result,
        const std::filesystem::path& path, const QString& fieldName,
        double minimum, double maximum, bool logarithmic);
    void updateDiagnostics();

    ImageView* m_imageView = nullptr;
    QLabel* m_probeLabel = nullptr;
    ColorBarWidget* m_colorBar = nullptr;
    QComboBox* m_fieldSelector = nullptr;
    QComboBox* m_levelSelector = nullptr;
    QComboBox* m_normalSelector = nullptr;
    QComboBox* m_rangeMode = nullptr;
    QCheckBox* m_logarithmic = nullptr;
    QCheckBox* m_gridBoxes = nullptr;
    QDoubleSpinBox* m_rangeMinimum = nullptr;
    QDoubleSpinBox* m_rangeMaximum = nullptr;
    QDoubleSpinBox* m_slicePosition = nullptr;
    QSlider* m_sliceSlider = nullptr;
    QTimer* m_sliceDebounce = nullptr;
    QTreeWidget* m_metadataTree = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    std::shared_ptr<PlotfileDataset> m_dataset;
    ScalarPlane m_displayPlane;
    int m_displayDimension = 0;
    int m_displayNormalDirection = 2;
    std::filesystem::path m_datasetPath;
    std::stop_source m_sliceStopSource;
    bool m_controlsReady = false;
    std::uint64_t m_generation = 0;
    std::uint64_t m_sliceGeneration = 0;
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
};

} // namespace amrvis::qt
