// ABOUTME: Tests ImageView mouse gestures through delivered events: probes,
// ABOUTME: line plots, and slice moves.

#include "ImageView.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QPoint>
#include <QSignalSpy>
#include <QtTest>

using amrvis::qt::ImageView;

namespace {

QImage solidImage()
{
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::darkGray);
    return image;
}

bool showView(ImageView& view)
{
    view.resize(400, 400);
    view.show();
    return QTest::qWaitForWindowExposed(&view);
}

QPoint viewCenter(const ImageView& view)
{
    return view.viewport()->rect().center();
}

} // namespace

class TestImageView final : public QObject
{
    Q_OBJECT

private slots:
    void leftClickProbes();
    void middleClickWithoutShiftDoesNotPlot();
    void rightClickWithoutShiftDoesNotPlot();
    void shiftMiddleClickRequestsLinePlot();
    void shiftRightClickRequestsLinePlot();
    void middleClickMovesSliceWhenSliceMoveEnabled();
    void rightClickMovesSliceWhenSliceMoveEnabled();
    void shiftMiddleClickKeepsLinePlotOverride();
    void shiftRightClickKeepsLinePlotOverride();
    void middleClickWithControlMovesSlice();
};

void TestImageView::leftClickProbes()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy probeSpy(&view, &ImageView::probeClicked);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        viewCenter(view));
    QTRY_COMPARE(probeSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
}

void TestImageView::middleClickWithoutShiftDoesNotPlot()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::NoModifier,
        viewCenter(view));
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
    QCOMPARE(sliceSpy.count(), 0);
}

void TestImageView::rightClickWithoutShiftDoesNotPlot()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
        viewCenter(view));
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
    QCOMPARE(sliceSpy.count(), 0);
}

void TestImageView::shiftMiddleClickRequestsLinePlot()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::ShiftModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
}

void TestImageView::shiftRightClickRequestsLinePlot()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::ShiftModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::RightButton);
}

void TestImageView::middleClickMovesSliceWhenSliceMoveEnabled()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::NoModifier,
        viewCenter(view));
    QTRY_COMPARE(sliceSpy.count(), 1);
    QCOMPARE(sliceSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
}

void TestImageView::rightClickMovesSliceWhenSliceMoveEnabled()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
        viewCenter(view));
    QTRY_COMPARE(sliceSpy.count(), 1);
    QCOMPARE(sliceSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::RightButton);
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
}

void TestImageView::shiftMiddleClickKeepsLinePlotOverride()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::ShiftModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(sliceSpy.count(), 0);
}

void TestImageView::shiftRightClickKeepsLinePlotOverride()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::ShiftModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(sliceSpy.count(), 0);
}

void TestImageView::middleClickWithControlMovesSlice()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::ControlModifier,
        viewCenter(view));
    QTRY_COMPARE(sliceSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
}

QTEST_MAIN(TestImageView)
#include "test_image_view.moc"
