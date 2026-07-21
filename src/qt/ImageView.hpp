#pragma once

#include <QGraphicsView>
#include <QColor>
#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QRectF>

#include <optional>
#include <vector>

class QGraphicsLineItem;
class QGraphicsPathItem;
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

struct OverlaySegment {
    QLineF line;
    QColor color;
    float width = 1.0F;
};

struct OverlayPath {
    QPainterPath path;
    QColor color;
    float width = 1.0F;
};

class ImageView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setGridBoxes(const std::vector<GridBoxOverlay>& boxes);
    void setOverlaySegments(const std::vector<OverlaySegment>& segments);
    // Smooth contour polylines, rendered as cosmetic-pen path items at the
    // same z (2) as the overlay segments. Only replaces the path items; the
    // segment items are untouched, so callers that switch overlay kinds must
    // also clear the other setter. setImage/setPlaceholder drop both.
    void setOverlayPaths(const std::vector<OverlayPath>& paths);
    // Crosshair guides spanning the whole image, used by the 3-D slice views
    // to mark where the other two slice planes intersect this one. The lines
    // are in scene coordinates; a nullopt line hides that guide. They layer
    // at z 1.5, between the grid boxes (z 1) and the overlay segments (z 2).
    void setCrosshairs(const std::optional<QLineF>& vertical,
        const std::optional<QLineF>& horizontal, const QColor& verticalColor,
        const QColor& horizontalColor);
    // Small L-shaped axis indicator painted in the lower-left corner of the
    // viewport (not the scene), so it stays fixed regardless of zoom or pan.
    void setAxisIndicator(const QString& horizontal, const QString& vertical);
    // Cosmetic red rectangle marking the cell picked in the dataset window;
    // std::nullopt clears it, and setImage/setPlaceholder drop it too. It
    // layers at z 4, above the overlay segments.
    void setCellHighlight(const std::optional<QRectF>& sceneRect);
    void setPlaceholder(const QString& text);
    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] const QImage& image() const noexcept;
    // Renders the scene (base image plus grid boxes and any other overlays)
    // to a fresh QImage for export. scaleFactor multiplies the raster's native
    // resolution so the export reflects the on-screen zoom (WYSIWYG); an
    // aspect-preserving cap keeps extreme zooms from allocating gigabytes.
    [[nodiscard]] QImage composedImage(qreal scaleFactor = 1.0) const;
    void fitToWindow();
    void setFixedScale(int factor);
    void zoomToRect(const QRectF& sceneRect);
    // A middle/right click (or drag) with Shift held emits linePlotRequested
    // (x-line for middle, y-line for right) in both 2-D and 3-D. Without
    // Shift, slice-move-enabled views (the 3-D slice views) emit
    // sliceMoveRequested instead; in 2-D the unmodified click does nothing.
    void setSliceMoveEnabled(bool enabled) noexcept;

signals:
    void probeMoved(int x, int y);
    void probeClicked(int x, int y);
    void rubberBandSelected(const QRectF& sceneRect);
    void linePlotRequested(int imageX, int imageY, Qt::MouseButton button);
    void sliceMoveRequested(int imageX, int imageY, Qt::MouseButton button);
    void fitRequested();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void fitImage();
    void updateLineGuide(const QPoint& viewPosition);
    void clearLineGuide();
    void applyCrosshairs();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    QImage m_image;
    std::vector<QGraphicsRectItem*> m_gridItems;
    std::vector<QGraphicsLineItem*> m_overlayItems;
    std::vector<QGraphicsPathItem*> m_pathItems;
    std::optional<QLineF> m_crosshairVertical;
    std::optional<QLineF> m_crosshairHorizontal;
    QColor m_crosshairVerticalColor;
    QColor m_crosshairHorizontalColor;
    QGraphicsLineItem* m_crosshairVerticalItem = nullptr;
    QGraphicsLineItem* m_crosshairHorizontalItem = nullptr;
    QGraphicsRectItem* m_cellHighlightItem = nullptr;
    QString m_indicatorH;
    QString m_indicatorV;
    QPoint m_pressPosition;
    Qt::MouseButton m_lineDragButton = Qt::NoButton;
    QPoint m_linePressPosition;
    QGraphicsLineItem* m_lineGuide = nullptr;
    bool m_sliceMoveEnabled = false;
    bool m_fitOnResize = true;
};

} // namespace amrvis::qt
