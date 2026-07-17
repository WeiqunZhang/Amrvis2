#include "ImageView.hpp"

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QPen>

#include <cmath>

namespace amrvis::qt {

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setAlignment(Qt::AlignCenter);
    setBackgroundBrush(palette().window());
    setDragMode(QGraphicsView::ScrollHandDrag);
    setMouseTracking(true);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void ImageView::setImage(const QImage& image)
{
    m_scene->clear();
    m_gridItems.clear();
    m_image = image;
    m_item = m_scene->addPixmap(QPixmap::fromImage(m_image));
    m_scene->setSceneRect(m_item->boundingRect());
    m_fitOnResize = true;
    fitImage();
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
        auto* item = m_scene->addRect(box.rectangle, pen, Qt::NoBrush);
        item->setZValue(1.0);
        m_gridItems.push_back(item);
    }
}

void ImageView::setPlaceholder(const QString& text)
{
    m_scene->clear();
    m_gridItems.clear();
    m_item = nullptr;
    m_image = {};
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

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (hasImage()) {
        m_fitOnResize = true;
        fitImage();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);
    if (!hasImage()) {
        return;
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

void ImageView::fitImage()
{
    if (m_item != nullptr) {
        resetTransform();
        fitInView(m_item, Qt::KeepAspectRatio);
    }
}

} // namespace amrvis::qt
