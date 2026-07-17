#include "ColorBarWidget.hpp"

#include <amrvis/render2d/ScalarRenderer.hpp>

#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <utility>

namespace amrvis::qt {

ColorBarWidget::ColorBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(150, 280);
}

void ColorBarWidget::setFieldRange(QString fieldName, double minimum, double maximum)
{
    m_fieldName = std::move(fieldName);
    m_minimum = minimum;
    m_maximum = maximum;
    m_hasRange = true;
    update();
}

void ColorBarWidget::clearRange()
{
    m_fieldName.clear();
    m_hasRange = false;
    update();
}

void ColorBarWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().windowText().color());

    if (!m_hasRange) {
        painter.drawText(rect().adjusted(8, 8, -8, -8),
            Qt::AlignCenter | Qt::TextWordWrap, tr("No scalar range"));
        return;
    }

    constexpr int margin = 12;
    constexpr int titleHeight = 30;
    constexpr int labelWidth = 82;
    const QRect bar(margin, margin + titleHeight, 28,
        std::max(1, height() - 2 * margin - titleHeight));
    painter.drawText(QRect(margin, margin, width() - 2 * margin, titleHeight),
        Qt::AlignLeft | Qt::AlignVCenter, m_fieldName);

    QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
    constexpr int samples = 16;
    for (int sample = 0; sample <= samples; ++sample) {
        const auto normalized = static_cast<double>(sample) / samples;
        const auto color = sampleViridis(1.0 - normalized);
        gradient.setColorAt(normalized, QColor(color[0], color[1], color[2]));
    }
    painter.fillRect(bar, gradient);
    painter.drawRect(bar.adjusted(0, 0, -1, -1));

    const QRect labels(bar.right() + 8, bar.top(), labelWidth, bar.height());
    painter.drawText(labels, Qt::AlignLeft | Qt::AlignTop,
        QString::number(m_maximum, 'g', 6));
    painter.drawText(labels, Qt::AlignLeft | Qt::AlignBottom,
        QString::number(m_minimum, 'g', 6));
}

} // namespace amrvis::qt
