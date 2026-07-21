// ABOUTME: Tests ImageView mouse gestures through delivered events: probes,
// ABOUTME: line plots, slice moves, and the macOS middle-click emulation.

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
    void middleClickRequestsLinePlot();
    void middleClickMovesSliceWhenSliceMoveEnabled();
    void middleClickWithControlKeepsLinePlotOverride();
#ifdef Q_OS_MAC
    void emulatedClickRequestsLinePlot_data();
    void emulatedClickRequestsLinePlot();
    void emulatedClickMovesSliceWhenSliceMoveEnabled_data();
    void emulatedClickMovesSliceWhenSliceMoveEnabled();
    void shiftOptionClickKeepsLinePlotOverride();
    void modifierReleasedBeforeButtonStillEmits();
    void emulatedDragSuppressesRubberBand();
#endif
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

void TestImageView::middleClickRequestsLinePlot()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::NoModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
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

void TestImageView::middleClickWithControlKeepsLinePlotOverride()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::ControlModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(sliceSpy.count(), 0);
}

#ifdef Q_OS_MAC
void TestImageView::emulatedClickRequestsLinePlot_data()
{
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::newRow("option-click") << Qt::KeyboardModifiers(Qt::AltModifier);
    QTest::newRow("command-click") << Qt::KeyboardModifiers(Qt::ControlModifier);
}

void TestImageView::emulatedClickRequestsLinePlot()
{
    QFETCH(Qt::KeyboardModifiers, modifiers);
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy probeSpy(&view, &ImageView::probeClicked);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
    QCoreApplication::processEvents();
    QCOMPARE(probeSpy.count(), 0);
}

void TestImageView::emulatedClickMovesSliceWhenSliceMoveEnabled_data()
{
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::newRow("option-click") << Qt::KeyboardModifiers(Qt::AltModifier);
    QTest::newRow("command-click") << Qt::KeyboardModifiers(Qt::ControlModifier);
}

void TestImageView::emulatedClickMovesSliceWhenSliceMoveEnabled()
{
    QFETCH(Qt::KeyboardModifiers, modifiers);
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, viewCenter(view));
    // A plain emulated middle click must act like a plain middle click even
    // though Command reports as ControlModifier, the line-plot override.
    QTRY_COMPARE(sliceSpy.count(), 1);
    QCOMPARE(sliceSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
    QCoreApplication::processEvents();
    QCOMPARE(lineSpy.count(), 0);
}

void TestImageView::shiftOptionClickKeepsLinePlotOverride()
{
    ImageView view;
    view.setImage(solidImage());
    view.setSliceMoveEnabled(true);
    QVERIFY(showView(view));
    QSignalSpy sliceSpy(&view, &ImageView::sliceMoveRequested);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mouseClick(view.viewport(), Qt::LeftButton,
        Qt::AltModifier | Qt::ShiftModifier, viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
    QCoreApplication::processEvents();
    QCOMPARE(sliceSpy.count(), 0);
}

void TestImageView::modifierReleasedBeforeButtonStillEmits()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::AltModifier,
        viewCenter(view));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        viewCenter(view));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.takeFirst().at(2).value<Qt::MouseButton>(), Qt::MiddleButton);
}

void TestImageView::emulatedDragSuppressesRubberBand()
{
    ImageView view;
    view.setImage(solidImage());
    QVERIFY(showView(view));
    QSignalSpy rubberBandSpy(&view, &ImageView::rubberBandSelected);
    QSignalSpy lineSpy(&view, &ImageView::linePlotRequested);
    const auto center = viewCenter(view);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::AltModifier,
        center - QPoint(40, 40));
    QTest::mouseMove(view.viewport(), center + QPoint(40, 40));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::AltModifier,
        center + QPoint(40, 40));
    QTRY_COMPARE(lineSpy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(rubberBandSpy.count(), 0);
}
#endif

QTEST_MAIN(TestImageView)
#include "test_image_view.moc"
