#pragma once

#include <QString>
#include <QWidget>

namespace amrvis::qt {

class ColorBarWidget final : public QWidget {
public:
    explicit ColorBarWidget(QWidget* parent = nullptr);

    void setFieldRange(QString fieldName, double minimum, double maximum);
    void clearRange();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_fieldName;
    double m_minimum = 0.0;
    double m_maximum = 1.0;
    bool m_hasRange = false;
};

} // namespace amrvis::qt
