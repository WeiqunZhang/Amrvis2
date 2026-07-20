#pragma once

#include <QDialog>

#include <string>
#include <utility>
#include <vector>

class QButtonGroup;
class QComboBox;
class QGroupBox;
class QSpinBox;

namespace amrvis::qt {

enum class DisplayMode {
    Raster,
    RasterContours,
    VelocityVectors
};

// Contour color selection: black, white, or a specific palette index.
// Stored as an int: -1 = black (default), -2 = white, 0–255 = palette slot.
constexpr int contourColorBlack = -1;
constexpr int contourColorWhite = -2;

// Legacy vector-field defaults: a case-insensitive "x_velocity"/"y_velocity"
// substring match, then an exact "u"/"v" field name, else the first two
// fields.
[[nodiscard]] std::pair<int, int> detectVectorFields(
    const std::vector<std::string>& fieldNames);

class SetContoursDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SetContoursDialog(const std::vector<std::string>& fieldNames,
        QWidget* parent = nullptr);

    void setMode(DisplayMode mode);
    void setContourCount(int count);
    void setVectorFields(int uField, int vField);
    void setContourColor(int color);
    [[nodiscard]] DisplayMode mode() const;
    [[nodiscard]] int contourCount() const;
    [[nodiscard]] int uField() const;
    [[nodiscard]] int vField() const;
    [[nodiscard]] int contourColor() const;

signals:
    void applied();

private:
    QButtonGroup* m_modeButtons = nullptr;
    QSpinBox* m_contourCount = nullptr;
    QGroupBox* m_vectorBox = nullptr;
    QComboBox* m_uField = nullptr;
    QComboBox* m_vField = nullptr;
    QComboBox* m_contourColorCombo = nullptr;
    QSpinBox* m_colorIndex = nullptr;
};

} // namespace amrvis::qt
