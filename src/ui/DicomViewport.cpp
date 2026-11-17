#include "ui/DicomViewport.h"
#include "core/DicomLoader.h"

#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace dicom {

namespace {

QString oppositeAnatomyCode(const QString& code) {
    QString opp;
    for (const QChar ch : code) {
        switch (ch.toUpper().toLatin1()) {
        case 'A': opp.append(QLatin1Char('P')); break;
        case 'P': opp.append(QLatin1Char('A')); break;
        case 'L': opp.append(QLatin1Char('R')); break;
        case 'R': opp.append(QLatin1Char('L')); break;
        case 'H': opp.append(QLatin1Char('F')); break;
        case 'F': opp.append(QLatin1Char('H')); break;
        default:  opp.append(ch); break;
        }
    }
    return opp;
}

} // namespace

DicomViewport::DicomViewport(QWidget* parent)
    : QGraphicsView(parent) {
    setObjectName(QStringLiteral("dicomGraphicsView"));

    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QColor(10, 10, 10));
    setScene(m_scene);

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QStringLiteral("border: 1px solid #2c2c2c; background-color: #0a0a0a;"));
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setToolTip(QStringLiteral("Roda: Zoom | Esq: Mover | Dir + arraste: WL/WW | Régua/ROI: medir"));

    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_scene->addItem(m_pixmapItem);
}

void DicomViewport::setImageData(const DicomImageData& data, bool resetViewTransforms) {
    m_data = data;
    if (resetViewTransforms) {
        clearMeasurements();
        m_isInverted = false;
        m_rotationQuarterTurns = 0;
        m_flipHorizontal = false;
    }
    refreshPixmap();
    updateOrientationOverlay();
    if (resetViewTransforms) {
        fitToWindow();
    }
}

void DicomViewport::updateImageDataKeepView(const DicomImageData& data) {
    m_data = data;
    refreshPixmap();
}

void DicomViewport::setInverted(bool inverted) {
    m_isInverted = inverted;
    refreshPixmap();
}

void DicomViewport::rotate90Clockwise() {
    if (!hasImage()) return;
    m_rotationQuarterTurns = (m_rotationQuarterTurns + 1) % 4;
    refreshPixmap();
    updateOrientationOverlay();
    if (m_autoFitEnabled) {
        fitToWindow();
    }
}

void DicomViewport::toggleFlipHorizontal() {
    if (!hasImage()) return;
    m_flipHorizontal = !m_flipHorizontal;
    refreshPixmap();
    updateOrientationOverlay();
}

void DicomViewport::setInteractionMode(InteractionMode mode) {
    m_mode = mode;
    m_hasMeasureStart = false;
    clearPreviewItems();

    const bool drawing = (mode != InteractionMode::Navigate);
    setDragMode(drawing ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    viewport()->setCursor(drawing ? Qt::CrossCursor : Qt::ArrowCursor);
}

void DicomViewport::zoomIn() {
    if (!hasImage()) return;
    m_autoFitEnabled = false;
    scale(1.25, 1.25);
}

void DicomViewport::zoomOut() {
    if (!hasImage()) return;
    m_autoFitEnabled = false;
    scale(0.8, 0.8);
}

void DicomViewport::resetZoom() {
    if (!hasImage()) return;
    m_autoFitEnabled = false;
    resetTransform();
}

void DicomViewport::fitToWindow() {
    if (!hasImage()) return;
    m_autoFitEnabled = true;
    resetTransform();
    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

void DicomViewport::refreshPixmap() {
    if (!m_data.isValid) return;

    QImage img = m_data.image;
    if (m_isInverted) {
        img.invertPixels(QImage::InvertRgb);
    }
    if (m_flipHorizontal) {
        QTransform flip;
        flip.scale(-1.0, 1.0);
        img = img.transformed(flip);
    }
    if (m_rotationQuarterTurns != 0) {
        QTransform rot;
        rot.rotate(static_cast<qreal>(m_rotationQuarterTurns * 90));
        img = img.transformed(rot, Qt::SmoothTransformation);
    }

    const QPixmap pixmap = QPixmap::fromImage(img);
    m_pixmapItem->setPixmap(pixmap);
    m_scene->setSceneRect(pixmap.rect());
}

void DicomViewport::updateOrientationOverlay() {
    removeSceneItems(m_orientationItems);
    if (!m_data.isValid || m_data.patientOrientation.isEmpty()) return;

    const QStringList parts = m_data.patientOrientation.split(QLatin1Char('\\'));
    if (parts.size() < 2) return;

    QString rightLabel = parts[0].trimmed();
    QString leftLabel = oppositeAnatomyCode(rightLabel);
    QString bottomLabel = parts[1].trimmed();
    QString topLabel = oppositeAnatomyCode(bottomLabel);

    if (m_flipHorizontal) {
        std::swap(leftLabel, rightLabel);
    }
    for (int r = 0; r < m_rotationQuarterTurns; ++r) {
        const QString prevTop = topLabel;
        topLabel = leftLabel;
        leftLabel = bottomLabel;
        bottomLabel = rightLabel;
        rightLabel = prevTop;
    }

    const QRectF rect = m_scene->sceneRect();
    if (rect.isEmpty()) return;

    auto addMarker = [this](const QString& text, const QPointF& pos) {
        if (text.isEmpty()) return;
        auto* item = m_scene->addSimpleText(text);
        item->setBrush(QColor(255, 213, 79));
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        item->setPos(pos);
        item->setZValue(9.0);
        m_orientationItems.append(item);
    };

    const qreal margin = std::max<qreal>(12.0, std::min(rect.width(), rect.height()) * 0.02);
    addMarker(topLabel, QPointF(rect.center().x(), rect.top() + margin));
    addMarker(bottomLabel, QPointF(rect.center().x(), rect.bottom() - margin));
    addMarker(leftLabel, QPointF(rect.left() + margin, rect.center().y()));
    addMarker(rightLabel, QPointF(rect.right() - margin, rect.center().y()));
}

QPoint DicomViewport::mapScenePointToOriginalPixel(const QPointF& scenePt) const {
    if (!m_data.isValid || m_data.width <= 0 || m_data.height <= 0) {
        return QPoint(-1, -1);
    }

    const int w0 = m_data.width;
    const int h0 = m_data.height;
    const int dx = static_cast<int>(std::floor(scenePt.x()));
    const int dy = static_cast<int>(std::floor(scenePt.y()));

    int px = dx;
    int py = dy;
    switch (m_rotationQuarterTurns % 4) {
    case 1: px = dy;          py = h0 - 1 - dx; break;
    case 2: px = w0 - 1 - dx; py = h0 - 1 - dy; break;
    case 3: px = w0 - 1 - dy; py = dx;          break;
    default: break;
    }

    if (m_flipHorizontal) {
        px = w0 - 1 - px;
    }
    return QPoint(px, py);
}

double DicomViewport::measureDistanceMm(const QPointF& p1, const QPointF& p2) {
    const bool swappedAxes = (m_rotationQuarterTurns % 2) != 0;
    const double spacingX = swappedAxes ? m_data.pixelSpacingY : m_data.pixelSpacingX;
    const double spacingY = swappedAxes ? m_data.pixelSpacingX : m_data.pixelSpacingY;
    const double distMm = std::hypot((p2.x() - p1.x()) * spacingX,
                                     (p2.y() - p1.y()) * spacingY);

    QPen pen(QColor(0, 230, 118), 2.0);
    pen.setCosmetic(true);
    auto* lineItem = m_scene->addLine(QLineF(p1, p2), pen);
    lineItem->setZValue(10.0);

    auto* labelItem = m_scene->addSimpleText(QStringLiteral("%1 mm").arg(distMm, 0, 'f', 1));
    labelItem->setBrush(QColor(0, 230, 118));
    labelItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    labelItem->setPos((p1 + p2) * 0.5);
    labelItem->setZValue(11.0);

    m_measurementItems.append(lineItem);
    m_measurementItems.append(labelItem);
    return distMm;
}

RoiStatistics DicomViewport::measureRoiStats(const QRectF& sceneRect) {
    const QRectF normRect = sceneRect.normalized();
    const QPoint origA = mapScenePointToOriginalPixel(normRect.topLeft());
    const QPoint origB = mapScenePointToOriginalPixel(
        QPointF(normRect.right() - 1.0, normRect.bottom() - 1.0));
    const QRect pixelRect = QRect(origA, origB).normalized();
    const RoiStatistics stats = DicomLoader::computeRoiStatistics(m_data, pixelRect);
    if (!stats.valid) return stats;

    QPen pen(QColor(255, 213, 79), 2.0);
    pen.setCosmetic(true);
    auto* rectItem = m_scene->addRect(normRect, pen);
    rectItem->setZValue(10.0);

    const QString text = QStringLiteral("Média: %1 | DP: %2\nMin/Max: %3 / %4\nÁrea: %5 mm²")
                             .arg(stats.mean, 0, 'f', 1)
                             .arg(stats.stdDev, 0, 'f', 1)
                             .arg(stats.minVal, 0, 'f', 0)
                             .arg(stats.maxVal, 0, 'f', 0)
                             .arg(stats.areaMm2, 0, 'f', 1);
    auto* labelItem = m_scene->addSimpleText(text);
    labelItem->setBrush(QColor(255, 213, 79));
    labelItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    labelItem->setPos(normRect.topLeft());
    labelItem->setZValue(11.0);

    m_measurementItems.append(rectItem);
    m_measurementItems.append(labelItem);
    return stats;
}

void DicomViewport::clearMeasurements() {
    clearPreviewItems();
    removeSceneItems(m_measurementItems);
    m_hasMeasureStart = false;
}

void DicomViewport::clearPreviewItems() {
    if (m_previewLineItem) {
        m_scene->removeItem(m_previewLineItem);
        delete m_previewLineItem;
        m_previewLineItem = nullptr;
    }
    if (m_previewRectItem) {
        m_scene->removeItem(m_previewRectItem);
        delete m_previewRectItem;
        m_previewRectItem = nullptr;
    }
}

void DicomViewport::removeSceneItems(QList<QGraphicsItem*>& items) {
    for (QGraphicsItem* item : items) {
        m_scene->removeItem(item);
    }
    qDeleteAll(items);
    items.clear();
}

void DicomViewport::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (m_autoFitEnabled && hasImage()) {
        fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}

void DicomViewport::wheelEvent(QWheelEvent* event) {
    if (hasImage() && event->angleDelta().y() != 0) {
        m_autoFitEnabled = false;
        const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        scale(factor, factor);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void DicomViewport::mousePressEvent(QMouseEvent* event) {
    if (!hasImage()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton && m_mode == InteractionMode::MeasureDistance) {
        const QPointF scenePos = mapToScene(event->pos());
        if (!m_hasMeasureStart) {
            m_measureStartScene = scenePos;
            m_hasMeasureStart = true;
            QPen previewPen(QColor(255, 235, 59), 1.5, Qt::DashLine);
            previewPen.setCosmetic(true);
            m_previewLineItem = m_scene->addLine(QLineF(scenePos, scenePos), previewPen);
        } else {
            clearPreviewItems();
            const double distMm = measureDistanceMm(m_measureStartScene, scenePos);
            m_hasMeasureStart = false;
            emit statusNotification(QStringLiteral("Medição registrada: %1 mm").arg(distMm, 0, 'f', 2));
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_mode == InteractionMode::MeasureRoi) {
        const QPointF scenePos = mapToScene(event->pos());
        if (!m_hasMeasureStart) {
            m_measureStartScene = scenePos;
            m_hasMeasureStart = true;
            QPen previewPen(QColor(255, 213, 79), 1.5, Qt::DashLine);
            previewPen.setCosmetic(true);
            m_previewRectItem = m_scene->addRect(QRectF(scenePos, scenePos), previewPen);
        } else {
            clearPreviewItems();
            const RoiStatistics stats = measureRoiStats(QRectF(m_measureStartScene, scenePos));
            m_hasMeasureStart = false;
            if (stats.valid) {
                emit statusNotification(
                    QStringLiteral("ROI: Média=%1 | DP=%2 | Min=%3 | Max=%4 | Área=%5 mm²")
                        .arg(stats.mean, 0, 'f', 1)
                        .arg(stats.stdDev, 0, 'f', 1)
                        .arg(stats.minVal, 0, 'f', 0)
                        .arg(stats.maxVal, 0, 'f', 0)
                        .arg(stats.areaMm2, 0, 'f', 1));
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_data.isMonochrome) {
        m_isAdjustingWindow = true;
        m_lastMousePos = event->pos();
        viewport()->setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void DicomViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!hasImage()) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    if (m_mode == InteractionMode::MeasureDistance && m_hasMeasureStart && m_previewLineItem) {
        const QPointF scenePos = mapToScene(event->pos());
        m_previewLineItem->setLine(QLineF(m_measureStartScene, scenePos));
        const bool swappedAxes = (m_rotationQuarterTurns % 2) != 0;
        const double sx = swappedAxes ? m_data.pixelSpacingY : m_data.pixelSpacingX;
        const double sy = swappedAxes ? m_data.pixelSpacingX : m_data.pixelSpacingY;
        const double distMm = std::hypot((scenePos.x() - m_measureStartScene.x()) * sx,
                                         (scenePos.y() - m_measureStartScene.y()) * sy);
        emit statusNotification(QStringLiteral("Medindo: %1 mm").arg(distMm, 0, 'f', 1));
        event->accept();
        return;
    }

    if (m_mode == InteractionMode::MeasureRoi && m_hasMeasureStart && m_previewRectItem) {
        m_previewRectItem->setRect(QRectF(m_measureStartScene, mapToScene(event->pos())).normalized());
        event->accept();
        return;
    }

    if (m_isAdjustingWindow) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        const double dynamicStep = std::max(1.0, (m_data.maxPixelValue - m_data.minPixelValue) / 500.0);
        const double newWidth = std::max(1.0, m_data.windowWidth + delta.x() * dynamicStep);
        const double newCenter = m_data.windowCenter + delta.y() * dynamicStep;
        emit windowLevelAdjusted(newCenter, newWidth);
        event->accept();
        return;
    }

    const QPoint origPt = mapScenePointToOriginalPixel(mapToScene(event->pos()));
    double pixelVal = 0.0;
    if (DicomLoader::samplePixelValue(m_data, origPt.x(), origPt.y(), pixelVal)) {
        emit pixelHovered(origPt.x(), origPt.y(), pixelVal);
    }

    QGraphicsView::mouseMoveEvent(event);
}

void DicomViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isAdjustingWindow && event->button() == Qt::RightButton) {
        m_isAdjustingWindow = false;
        viewport()->setCursor((m_mode != InteractionMode::Navigate) ? Qt::CrossCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

bool DicomViewport::hasImage() const {
    return m_data.isValid && m_pixmapItem && !m_pixmapItem->pixmap().isNull();
}

QSize DicomViewport::displayedImageSize() const {
    return hasImage() ? m_pixmapItem->pixmap().size() : QSize();
}

QImage DicomViewport::displayedImage() const {
    return hasImage() ? m_pixmapItem->pixmap().toImage() : QImage();
}

QPixmap DicomViewport::displayedPixmap() const {
    return hasImage() ? m_pixmapItem->pixmap() : QPixmap();
}

} // namespace dicom
