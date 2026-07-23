#include "ScientificDoubleSpinBox.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QString>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

class TestScientificDoubleSpinBox : public amrvis::qt::ScientificDoubleSpinBox {
public:
    using ScientificDoubleSpinBox::validate;
    using ScientificDoubleSpinBox::valueFromText;

    [[nodiscard]] QLineEdit* editor() const
    {
        return lineEdit();
    }
};

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

QValidator::State validationState(
    TestScientificDoubleSpinBox& spinBox, QString text)
{
    auto position = static_cast<int>(text.size());
    return spinBox.validate(text, position);
}

} // namespace

int main(int argc, char* argv[])
{
    [[maybe_unused]] QApplication application(argc, argv);

    TestScientificDoubleSpinBox spinBox;
    spinBox.setRange(-std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max());
    spinBox.setPrefix(QStringLiteral("min "));

    require(!spinBox.keyboardTracking(),
        "range input should not commit while the user is typing");
    require(validationState(spinBox, QStringLiteral("min 1.2e-6"))
            == QValidator::Acceptable,
        "lowercase scientific notation should be accepted");
    require(validationState(spinBox, QStringLiteral("min +1.2E+6"))
            == QValidator::Acceptable,
        "uppercase scientific notation should be accepted");
    require(validationState(spinBox, QStringLiteral("min 1.2e-"))
            == QValidator::Intermediate,
        "an unfinished exponent should remain editable");
    require(validationState(spinBox, QStringLiteral("min 1.2e--6"))
            == QValidator::Invalid,
        "a malformed exponent should be rejected");
    require(spinBox.valueFromText(QStringLiteral("min 1.2e-6")) == 1.2e-6,
        "scientific notation should convert to the expected value");

    auto valueChanges = 0;
    QObject::connect(&spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged),
        [&valueChanges](double) { ++valueChanges; });
    spinBox.editor()->setText(QStringLiteral("min 2.5e-6"));
    spinBox.editor()->setModified(true);
    QApplication::processEvents();
    require(spinBox.value() == 0.0 && valueChanges == 0,
        "editing should not apply an otherwise valid partial value");
    spinBox.interpretText();
    require(spinBox.value() == 2.5e-6 && valueChanges == 1,
        "finishing an edit should apply the completed value once");

    spinBox.setNumberFormat(QStringLiteral("%.3e"));
    constexpr double tinyValue = 1.2e-100;
    spinBox.setValue(tinyValue);
    require(spinBox.value() == tinyValue,
        "display formatting should not change the stored value");
    require(spinBox.cleanText() == QStringLiteral("1.200e-100"),
        "range display should use the configured number format");

    spinBox.setNumberFormat(QStringLiteral("%g"));
    constexpr double preciseValue = 1.23456789012345;
    spinBox.setValue(preciseValue);
    spinBox.interpretText();
    require(spinBox.value() == preciseValue,
        "committing unchanged display text should preserve the stored value");

    spinBox.setNumberFormat(QStringLiteral("value=%.2e units"));
    spinBox.setValue(1.25);
    require(spinBox.cleanText() == QStringLiteral("1.25e+00"),
        "range display should ignore surrounding format text");
}
