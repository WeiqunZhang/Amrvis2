#pragma once

#include <QGraphicsView>
#include <QColor>
#include <QImage>
#include <QRectF>

#include <vector>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

namespace amrvis::qt {

struct GridBoxOverlay {
    QRectF rectangle;
    QColor color;
};

class ImageView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setGridBoxes(const std::vector<GridBoxOverlay>& boxes);
    void setPlaceholder(const QString& text);
    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] const QImage& image() const noexcept;

signals:
    void probeMoved(int x, int y);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void fitImage();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    QImage m_image;
    std::vector<QGraphicsRectItem*> m_gridItems;
    bool m_fitOnResize = true;
};

} // namespace amrvis::qt
