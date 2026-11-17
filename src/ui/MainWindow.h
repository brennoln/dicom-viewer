#pragma once

#include "core/AuditLogger.h"
#include "core/DicomImageData.h"
#include "core/DicomLoader.h"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

namespace dicom {

class DicomViewport;
class MetadataDockWidget;

/**
 * @brief Janela principal da aplicação Qt para clínica médica, orquestrando menus,
 * barras de controle (WL/WW e Multi-Frame), carregamento assíncrono, viewport e metadados.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool loadDicomFile(const QString& filePath, bool showModalError = true);
    void loadDicomFileAsync(const QString& filePath, bool showModalError = true);

    void setWindowLevelWidth(double center, double width);
    void applyClinicalWindowPreset(int presetIndex);
    void resetWindowLevelWidth();
    bool setCurrentFrame(int frameIndex);

    double measureDistanceMm(const QPointF& p1, const QPointF& p2);
    RoiStatistics measureRoiStats(const QRectF& sceneRect);

    // Métodos de inspeção para testes automatizados de interface (QtTest)
    bool hasLoadedImage() const;
    QSize displayedImageSize() const;
    QImage displayedImage() const;
    const QString& lastErrorMessage() const;
    QString statusSummaryText() const;
    bool isErrorBannerVisible() const;
    double currentWindowCenter() const;
    double currentWindowWidth() const;
    int metadataRowCount() const;
    bool isMetadataPanelVisible() const;
    bool isInverted() const;
    int measurementCount() const;
    int currentFrameIndex() const { return m_currentData.currentFrame; }
    int totalFrameCount() const { return m_currentData.frameCount; }
    bool isFrameBarVisible() const { return m_frameControlBar && m_frameControlBar->isVisible(); }
    bool isLoadingAsync() const { return m_loadWatcher && m_loadWatcher->isRunning(); }

signals:
    void asyncLoadCompleted(bool success);

public slots:
    void onOpenFileTriggered();
    void onExportImageTriggered();
    void onExportAnonymizedDicomTriggered();
    void onZoomIn();
    void onZoomOut();
    void onResetZoom();
    void onFitToWindow();
    void onResetWindowTriggered();
    void onInvertToggled(bool checked);
    void onRotate90Triggered();
    void onFlipHorizontalTriggered();
    void onMeasureModeToggled(bool checked);
    void onRoiModeToggled(bool checked);
    void onClearMeasurementsTriggered();
    void onCinePlayToggled(bool checked);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupActionsAndToolbar();
    void setupUi();
    bool applyLoadedData(const QString& filePath, const DicomImageData& data, bool showModalError);
    void updateInfoPanel(const QString& filePath, const DicomImageData& data);
    void updateWindowControls(const DicomImageData& data);
    void updateFrameControls(const DicomImageData& data);

    DicomLoader m_loader;
    AuditLogger m_auditLogger;
    DicomImageData m_currentData;
    QString m_currentFilePath;
    QString m_lastError;
    bool m_updatingWindowControls = false;
    bool m_updatingFrameControls = false;

    QFutureWatcher<DicomImageData>* m_loadWatcher = nullptr;
    QString m_pendingAsyncPath;
    bool m_pendingShowModalError = true;

    QAction* m_openAction = nullptr;
    QAction* m_exportImageAction = nullptr;
    QAction* m_exportAnonAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_invertAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_flipHAction = nullptr;
    QAction* m_measureAction = nullptr;
    QAction* m_roiAction = nullptr;
    QAction* m_clearMeasuresAction = nullptr;
    QAction* m_toggleMetadataAction = nullptr;

    QPushButton* m_openButton = nullptr;
    QLabel* m_infoHeaderLabel = nullptr;
    QLabel* m_errorBannerLabel = nullptr;

    QWidget* m_windowControlBar = nullptr;
    QComboBox* m_windowPresetCombo = nullptr;
    QSlider* m_wcSlider = nullptr;
    QDoubleSpinBox* m_wcSpinBox = nullptr;
    QSlider* m_wwSlider = nullptr;
    QDoubleSpinBox* m_wwSpinBox = nullptr;
    QPushButton* m_resetWindowButton = nullptr;

    QWidget* m_frameControlBar = nullptr;
    QPushButton* m_prevFrameButton = nullptr;
    QSlider* m_frameSlider = nullptr;
    QSpinBox* m_frameSpinBox = nullptr;
    QPushButton* m_nextFrameButton = nullptr;
    QLabel* m_frameTotalLabel = nullptr;
    QPushButton* m_cinePlayButton = nullptr;
    QTimer* m_cineTimer = nullptr;

    DicomViewport* m_viewport = nullptr;
    MetadataDockWidget* m_metadataDock = nullptr;
};

} // namespace dicom
