#include "SetContoursDialog.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <cctype>

namespace amrvis::qt {

std::pair<int, int> detectVectorFields(const std::vector<std::string>& fieldNames)
{
    const auto containsIgnoreCase = [&fieldNames](const char* needle) {
        for (std::size_t index = 0; index < fieldNames.size(); ++index) {
            std::string lowered = fieldNames[index];
            for (auto& character : lowered) {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            if (lowered.find(needle) != std::string::npos) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    const auto findExact = [&fieldNames](const char* name) {
        for (std::size_t index = 0; index < fieldNames.size(); ++index) {
            if (fieldNames[index] == name) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };

    auto uField = containsIgnoreCase("x_velocity");
    if (uField < 0) {
        uField = findExact("u");
    }
    auto vField = containsIgnoreCase("y_velocity");
    if (vField < 0) {
        vField = findExact("v");
    }
    if (fieldNames.empty()) {
        return {0, 0};
    }
    const auto last = static_cast<int>(fieldNames.size()) - 1;
    if (uField < 0) {
        uField = 0;
    }
    if (vField < 0) {
        vField = last < 1 ? last : 1;
    }
    return {uField, vField};
}

SetContoursDialog::SetContoursDialog(const std::vector<std::string>& fieldNames,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Set Contours"));
    setWindowFlags(Qt::Window);

    auto* layout = new QVBoxLayout(this);

    auto* modeBox = new QGroupBox(tr("Display mode"), this);
    auto* modeLayout = new QVBoxLayout(modeBox);
    m_modeButtons = new QButtonGroup(this);
    constexpr std::array<const char*, 3> modeLabels{
        "Raster", "Raster && Contours", "Velocity Vectors"};
    for (std::size_t index = 0; index < modeLabels.size(); ++index) {
        auto* button = new QRadioButton(tr(modeLabels[index]), modeBox);
        m_modeButtons->addButton(button, static_cast<int>(index));
        modeLayout->addWidget(button);
    }
    layout->addWidget(modeBox);

    auto* countRow = new QHBoxLayout;
    countRow->addWidget(new QLabel(tr("Number of lines:"), this));
    m_contourCount = new QSpinBox(this);
    m_contourCount->setRange(1, 99);
    m_contourCount->setValue(10);
    countRow->addWidget(m_contourCount);
    countRow->addStretch();
    layout->addLayout(countRow);

    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(new QLabel(tr("Contour color:"), this));
    m_contourColorCombo = new QComboBox(this);
    m_contourColorCombo->addItem(tr("Black"), contourColorBlack);
    m_contourColorCombo->addItem(tr("White"), contourColorWhite);
    m_contourColorCombo->addItem(tr("Palette index"), 0);
    colorRow->addWidget(m_contourColorCombo);
    m_colorIndex = new QSpinBox(this);
    m_colorIndex->setRange(0, 255);
    m_colorIndex->setValue(0);
    m_colorIndex->setEnabled(false);
    colorRow->addWidget(m_colorIndex);
    colorRow->addStretch();
    layout->addLayout(colorRow);
    connect(m_contourColorCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int index) {
            m_colorIndex->setEnabled(index == 2);
        });

    m_vectorBox = new QGroupBox(tr("Velocity vector fields"), this);
    auto* vectorLayout = new QFormLayout(m_vectorBox);
    m_uField = new QComboBox(m_vectorBox);
    m_vField = new QComboBox(m_vectorBox);
    for (const auto& name : fieldNames) {
        const auto label = QString::fromStdString(name);
        m_uField->addItem(label);
        m_vField->addItem(label);
    }
    vectorLayout->addRow(tr("U field:"), m_uField);
    vectorLayout->addRow(tr("V field:"), m_vField);
    layout->addWidget(m_vectorBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
        | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::clicked, this,
        [this, buttons](QAbstractButton* button) {
            const auto role = buttons->buttonRole(button);
            if (role == QDialogButtonBox::AcceptRole) {
                emit applied();
                accept();
            } else if (role == QDialogButtonBox::ApplyRole) {
                emit applied();
            } else {
                reject();
            }
        });
    layout->addWidget(buttons);

    connect(m_modeButtons, &QButtonGroup::idToggled, this,
        [this](int id, bool checked) {
            if (checked) {
                m_vectorBox->setEnabled(id == static_cast<int>(DisplayMode::VelocityVectors));
            }
        });

    const auto [uField, vField] = detectVectorFields(fieldNames);
    setMode(DisplayMode::Raster);
    setVectorFields(uField, vField);
}

void SetContoursDialog::setMode(DisplayMode mode)
{
    auto* button = m_modeButtons->button(static_cast<int>(mode));
    if (button != nullptr) {
        button->setChecked(true);
    }
    m_vectorBox->setEnabled(mode == DisplayMode::VelocityVectors);
}

void SetContoursDialog::setContourCount(int count)
{
    m_contourCount->setValue(count);
}

void SetContoursDialog::setVectorFields(int uField, int vField)
{
    if (uField >= 0 && uField < m_uField->count()) {
        m_uField->setCurrentIndex(uField);
    }
    if (vField >= 0 && vField < m_vField->count()) {
        m_vField->setCurrentIndex(vField);
    }
}

DisplayMode SetContoursDialog::mode() const
{
    return static_cast<DisplayMode>(m_modeButtons->checkedId());
}

int SetContoursDialog::contourCount() const
{
    return m_contourCount->value();
}

int SetContoursDialog::uField() const
{
    return m_uField->currentIndex();
}

int SetContoursDialog::vField() const
{
    return m_vField->currentIndex();
}

void SetContoursDialog::setContourColor(int color)
{
    if (color == contourColorWhite) {
        m_contourColorCombo->setCurrentIndex(1);
    } else if (color >= 0) {
        m_contourColorCombo->setCurrentIndex(2);
        m_colorIndex->setValue(color);
    } else {
        m_contourColorCombo->setCurrentIndex(0);
    }
}

int SetContoursDialog::contourColor() const
{
    if (m_contourColorCombo->currentIndex() == 0) return contourColorBlack;
    if (m_contourColorCombo->currentIndex() == 1) return contourColorWhite;
    return m_colorIndex->value();
}

} // namespace amrvis::qt
