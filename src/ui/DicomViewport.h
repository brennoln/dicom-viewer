#pragma once

#include "core/DicomImageData.h"

#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRectF>

namespace dicom {

/**
 * @brief Viewport radiológico baseado em QGraphicsView responsável pela renderização,
 * zoom/pan, transformações clínicas, marcadores anatômicos, régua (mm), ROI (HU) e WL/WW via mouse.
 */
class DicomViewport : public QGraphicsView {
    Q_OBJECT

public:
    enum class InteractionMode {
        Navigate,
        MeasureDistance,
        MeasureRoi
    };

    explicit DicomViewport(QWidget* parent = nullptr);
    ~DicomViewport() override = default;

    void setImageData(const DicomImageData& data, bool resetViewTransforms);
    void updateImageDataKeepView(const DicomImageData& data);

    void setInverted(bool inverted);
    void rotate90Clockwise();
    void toggleFlipHorizontal();
    void setInteractionMode(InteractionMode mode);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();

    double measureDistanceMm(const QPointF& p1, const QPointF& p2);
    RoiStatistics measureRoiStats(const QRectF& sceneRect);
    void clearMeasurements();

    bool hasImage() const;
    QSize displayedImageSize() const;
    QImage displayedImage() const;
    QPixmap displayedPixmap() const;
    bool isInverted() const { return m_isInverted; }
    int measurementCount() const { return m_measurementItems.size(); }

signals:
    void windowLevelAdjusted(double newCenter, double newWidth);
    void pixelHovered(int origX, int origY, double calibratedValue);
    void statusNotification(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void refreshPixmap();
    void updateOrientationOverlay();
    void clearPreviewItems();
    void removeSceneItems(QList<QGraphicsItem*>& items);
    QPoint mapScenePointToOriginalPixel(const QPointF& scenePt) const;

    DicomImageData m_data;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;

    bool m_autoFitEnabled = true;
    bool m_isInverted = false;
    int m_rotationQuarterTurns = 0;
    bool m_flipHorizontal = false;

    InteractionMode m_mode = InteractionMode::Navigate;
    bool m_hasMeasureStart = false;
    QPointF m_measureStartScene;
    QGraphicsLineItem* m_previewLineItem = nullptr;
    QGraphicsRectItem* m_previewRectItem = nullptr;
    QList<QGraphicsItem*> m_measurementItems;
    QList<QGraphicsItem*> m_orientationItems;

    bool m_isAdjustingWindow = false;
    QPoint m_lastMousePos;
};

} // namespace dicom
