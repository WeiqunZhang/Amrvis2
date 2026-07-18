#include "ImageView.hpp"

#include "Theme.hpp"

#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace amrvis::qt {
namespace {

// Grid box outlines drawn crisp (anti-aliasing off), independently of the
// image's smooth scaling. Without this, adjacent boxes that share an edge
// draw their 1px outlines twice and the anti-alias fringe darkens on the
// shared interior lines, so a single level's boxes look like several colors.
// Axis-aligned 1px lines stay clean with anti-aliasing off.
class CrispRectItem : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget = nullptr) override
    {
        const auto antialiasing = painter->testRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::Antialiasing, false);
        QGraphicsRectItem::paint(painter, option, widget);
        painter->setRenderHint(QPainter::Antialiasing, antialiasing);
    }
};

} // namespace

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setAlignment(Qt::AlignCenter);
    setBackgroundBrush(palette().window());
    setDragMode(QGraphicsView::RubberBandDrag);
    setMouseTracking(true);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void ImageView::setImage(const QImage& image)
{
    m_scene->clear();
    m_gridItems.clear();
    m_overlayItems.clear();
    m_pathItems.clear();
    m_crosshairVerticalItem = nullptr;
    m_crosshairHorizontalItem = nullptr;
    m_cellHighlightItem = nullptr;
    m_lineGuide = nullptr;
    m_lineDragButton = Qt::NoButton;
    m_image = image;
    m_item = m_scene->addPixmap(QPixmap::fromImage(m_image));
    m_scene->setSceneRect(m_item->boundingRect());
    setBackgroundBrush(viewportBackground());
    m_fitOnResize = true;
    fitImage();
    applyCrosshairs();
}

void ImageView::setGridBoxes(const std::vector<GridBoxOverlay>& boxes)
{
    for (auto* item : m_gridItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_gridItems.clear();
    if (!hasImage()) {
        return;
    }
    m_gridItems.reserve(boxes.size());
    for (const auto& box : boxes) {
        QPen pen(box.color);
        pen.setCosmetic(true);
        pen.setWidth(1);
        auto* item = new CrispRectItem(box.rectangle);
        item->setPen(pen);
        item->setBrush(Qt::NoBrush);
        item->setZValue(1.0);
        m_scene->addItem(item);
        m_gridItems.push_back(item);
    }
}

void ImageView::setOverlaySegments(const std::vector<OverlaySegment>& segments)
{
    for (auto* item : m_overlayItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_overlayItems.clear();
    if (!hasImage()) {
        return;
    }
    m_overlayItems.reserve(segments.size());
    for (const auto& segment : segments) {
        QPen pen(segment.color);
        pen.setCosmetic(true);
        pen.setWidthF(segment.width);
        auto* item = m_scene->addLine(segment.line, pen);
        item->setZValue(2.0);
        m_overlayItems.push_back(item);
    }
}

void ImageView::setOverlayPaths(const std::vector<OverlayPath>& paths)
{
    for (auto* item : m_pathItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_pathItems.clear();
    if (!hasImage()) {
        return;
    }
    m_pathItems.reserve(paths.size());
    for (const auto& overlay : paths) {
        QPen pen(overlay.color);
        pen.setCosmetic(true);
        pen.setWidthF(overlay.width);
        auto* item = m_scene->addPath(overlay.path, pen);
        item->setZValue(2.0);
        m_pathItems.push_back(item);
    }
}

void ImageView::setCrosshairs(const std::optional<QLineF>& vertical,
    const std::optional<QLineF>& horizontal, const QColor& verticalColor,
    const QColor& horizontalColor)
{
    m_crosshairVertical = vertical;
    m_crosshairHorizontal = horizontal;
    m_crosshairVerticalColor = verticalColor;
    m_crosshairHorizontalColor = horizontalColor;
    applyCrosshairs();
}

void ImageView::applyCrosshairs()
{
    for (auto* item : {m_crosshairVerticalItem, m_crosshairHorizontalItem}) {
        if (item != nullptr) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_crosshairVerticalItem = nullptr;
    m_crosshairHorizontalItem = nullptr;
    if (!hasImage()) {
        return;
    }
    const auto addGuide = [this](const QLineF& line, const QColor& color) {
        QPen pen(color);
        pen.setCosmetic(true);
        pen.setWidth(2);
        auto* item = m_scene->addLine(line, pen);
        item->setZValue(1.5);
        return item;
    };
    if (m_crosshairVertical.has_value()) {
        m_crosshairVerticalItem = addGuide(*m_crosshairVertical,
            m_crosshairVerticalColor);
    }
    if (m_crosshairHorizontal.has_value()) {
        m_crosshairHorizontalItem = addGuide(*m_crosshairHorizontal,
            m_crosshairHorizontalColor);
    }
}

void ImageView::setCellHighlight(const std::optional<QRectF>& sceneRect)
{
    if (m_cellHighlightItem != nullptr) {
        m_scene->removeItem(m_cellHighlightItem);
        delete m_cellHighlightItem;
        m_cellHighlightItem = nullptr;
    }
    if (!sceneRect.has_value() || !hasImage()) {
        return;
    }
    QPen pen(Qt::red);
    pen.setCosmetic(true);
    pen.setWidth(2);
    m_cellHighlightItem = m_scene->addRect(*sceneRect, pen, Qt::NoBrush);
    m_cellHighlightItem->setZValue(4.0);
}

void ImageView::setSliceMoveEnabled(bool enabled) noexcept
{
    m_sliceMoveEnabled = enabled;
}

void ImageView::setPlaceholder(const QString& text)
{
    m_scene->clear();
    m_gridItems.clear();
    m_overlayItems.clear();
    m_pathItems.clear();
    m_crosshairVertical.reset();
    m_crosshairHorizontal.reset();
    m_crosshairVerticalItem = nullptr;
    m_crosshairHorizontalItem = nullptr;
    m_cellHighlightItem = nullptr;
    m_lineGuide = nullptr;
    m_lineDragButton = Qt::NoButton;
    m_item = nullptr;
    m_image = {};
    setBackgroundBrush(palette().window());
    auto* label = m_scene->addText(text);
    label->setDefaultTextColor(palette().windowText().color());
    m_scene->setSceneRect(label->boundingRect());
    resetTransform();
}

bool ImageView::hasImage() const noexcept
{
    return m_item != nullptr && !m_image.isNull();
}

const QImage& ImageView::image() const noexcept
{
    return m_image;
}

void ImageView::fitToWindow()
{
    if (hasImage()) {
        m_fitOnResize = true;
        fitImage();
    }
}

void ImageView::setFixedScale(int factor)
{
    if (!hasImage() || factor < 1) {
        return;
    }
    m_fitOnResize = false;
    resetTransform();
    const auto scaleFactor = static_cast<double>(factor);
    scale(scaleFactor, scaleFactor);
}

void ImageView::zoomToRect(const QRectF& sceneRect)
{
    if (!hasImage() || sceneRect.isEmpty()) {
        return;
    }
    m_fitOnResize = false;
    fitInView(sceneRect, Qt::KeepAspectRatio);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (hasImage()) {
        fitToWindow();
        emit fitRequested();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->position().toPoint();
    }
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
        && hasImage()) {
        m_lineDragButton = event->button();
        m_linePressPosition = event->position().toPoint();
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (event->button() == m_lineDragButton && m_lineDragButton != Qt::NoButton) {
        const auto button = m_lineDragButton;
        m_lineDragButton = Qt::NoButton;
        clearLineGuide();
        if (!hasImage()) {
            return;
        }
        const auto releasePosition = event->position().toPoint();
        const auto drag = releasePosition - m_linePressPosition;
        constexpr int minimumDrag = 4;
        if (std::max(std::abs(drag.x()), std::abs(drag.y())) < minimumDrag) {
            return;
        }
        const auto imagePosition = m_item->mapFromScene(mapToScene(releasePosition));
        const auto x = std::clamp(static_cast<int>(std::floor(imagePosition.x())),
            0, m_image.width() - 1);
        const auto y = std::clamp(static_cast<int>(std::floor(imagePosition.y())),
            0, m_image.height() - 1);
        const auto linePlotModifiers
            = Qt::ShiftModifier | Qt::ControlModifier;
        if (m_sliceMoveEnabled
            && (event->modifiers() & linePlotModifiers) == Qt::NoModifier) {
            emit sliceMoveRequested(x, y, button);
        } else {
            emit linePlotRequested(x, y, button);
        }
        return;
    }
    if (event->button() != Qt::LeftButton || !hasImage()) {
        return;
    }
    const auto releasePosition = event->position().toPoint();
    const auto drag = releasePosition - m_pressPosition;
    constexpr int minimumDrag = 4;
    if (std::abs(drag.x()) > minimumDrag && std::abs(drag.y()) > minimumDrag) {
        emit rubberBandSelected(QRectF(mapToScene(m_pressPosition),
            mapToScene(releasePosition)).normalized());
        return;
    }
    const auto imagePosition = m_item->mapFromScene(mapToScene(releasePosition));
    const auto x = static_cast<int>(std::floor(imagePosition.x()));
    const auto y = static_cast<int>(std::floor(imagePosition.y()));
    if (x >= 0 && y >= 0 && x < m_image.width() && y < m_image.height()) {
        emit probeClicked(x, y);
    }
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);
    if (!hasImage()) {
        return;
    }
    if (m_lineDragButton != Qt::NoButton) {
        updateLineGuide(event->position().toPoint());
    }
    const auto scenePosition = mapToScene(event->position().toPoint());
    const auto imagePosition = m_item->mapFromScene(scenePosition);
    const auto x = static_cast<int>(std::floor(imagePosition.x()));
    const auto y = static_cast<int>(std::floor(imagePosition.y()));
    if (x >= 0 && y >= 0 && x < m_image.width() && y < m_image.height()) {
        emit probeMoved(x, y);
    }
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitOnResize) {
        fitImage();
    }
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    if (!hasImage()) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    constexpr double zoomStep = 1.15;
    const auto factor = event->angleDelta().y() >= 0 ? zoomStep : 1.0 / zoomStep;
    scale(factor, factor);
    m_fitOnResize = false;
    event->accept();
}

void ImageView::updateLineGuide(const QPoint& viewPosition)
{
    const auto drag = viewPosition - m_linePressPosition;
    constexpr int minimumDrag = 4;
    if (std::max(std::abs(drag.x()), std::abs(drag.y())) < minimumDrag) {
        return;
    }
    const auto scenePosition = mapToScene(viewPosition);
    const auto width = static_cast<double>(m_image.width());
    const auto height = static_cast<double>(m_image.height());
    QLineF line;
    if (m_lineDragButton == Qt::MiddleButton) {
        const auto y = std::clamp(scenePosition.y(), 0.0, height);
        line = QLineF(0.0, y, width, y);
    } else {
        const auto x = std::clamp(scenePosition.x(), 0.0, width);
        line = QLineF(x, 0.0, x, height);
    }
    if (m_lineGuide == nullptr) {
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        m_lineGuide = m_scene->addLine(line, pen);
        m_lineGuide->setZValue(3.0);
    } else {
        m_lineGuide->setLine(line);
    }
}

void ImageView::clearLineGuide()
{
    if (m_lineGuide != nullptr) {
        m_scene->removeItem(m_lineGuide);
        delete m_lineGuide;
        m_lineGuide = nullptr;
    }
}

void ImageView::fitImage()
{
    if (m_item != nullptr) {
        resetTransform();
        fitInView(m_item, Qt::KeepAspectRatio);
    }
}

} // namespace amrvis::qt
