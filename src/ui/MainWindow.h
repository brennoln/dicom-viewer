#pragma once

#include "core/AuditLogger.h"
#include "core/DicomImageData.h"
#include "core/DicomLoader.h"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

namespace dicom {

/**
 * @brief Janela principal da aplicação Qt para clínica médica: visualização DICOM,
 * carregamento assíncrono, multi-frame/cine, presets clínicos de WL/WW, medições (régua/ROI),
 * marcadores anatômicos, anonimização LGPD e auditoria.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /**
     * @brief Carrega um arquivo DICOM de forma síncrona e atualiza a interface.
     */
    bool loadDicomFile(const QString& filePath, bool showModalError = true);

    /**
     * @brief Carrega um arquivo DICOM em background via QtConcurrent sem bloquear a thread da UI.
     */
    void loadDicomFileAsync(const QString& filePath, bool showModalError = true);

    /**
     * @brief Aplica novos valores de Window Level / Window Width na imagem exibida.
     */
    void setWindowLevelWidth(double center, double width);

    /**
     * @brief Aplica um preset clínico de janelamento (0=Padrão, 1=Pulmão, 2=Osso, 3=Abdômen, 4=Cérebro).
     */
    void applyClinicalWindowPreset(int presetIndex);

    /**
     * @brief Restaura os parâmetros padrão (VOI LUT / Window original) da imagem.
     */
    void resetWindowLevelWidth();

    /**
     * @brief Navega para um frame específico em exames multi-frame.
     */
    bool setCurrentFrame(int frameIndex);

    /**
     * @brief Adiciona uma medição de distância entre dois pontos da cena e retorna o valor em mm.
     */
    double measureDistanceMm(const QPointF& p1, const QPointF& p2);

    /**
     * @brief Adiciona uma Região de Interesse (ROI) e retorna suas estatísticas quantitativas (HU/área).
     */
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
    bool isInverted() const { return m_isInverted; }
    int measurementCount() const { return m_measurementItems.size(); }
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
    void onMetadataFilterChanged(const QString& filterText);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUi();
    void setupActionsAndToolbar();
    void setupMetadataDock();
    bool applyLoadedData(const QString& filePath, const DicomImageData& data, bool showModalError);
    void applyZoom(double factor);
    void updateInfoPanel(const QString& filePath, const DicomImageData& data);
    void updateWindowControls(const DicomImageData& data);
    void updateFrameControls(const DicomImageData& data);
    void updateMetadataTable(const QVector<DicomMetadataEntry>& entries);
    void updateOrientationOverlay();
    void refreshDisplayedPixmap();
    void clearMeasurements();

    DicomLoader m_loader;
    AuditLogger m_auditLogger;
    DicomImageData m_currentData;
    QString m_currentFilePath;
    QString m_lastError;
    bool m_autoFitEnabled = true;
    bool m_updatingWindowControls = false;
    bool m_updatingFrameControls = false;

    // Carregamento assíncrono em thread separada
    QFutureWatcher<DicomImageData>* m_loadWatcher = nullptr;
    QString m_pendingAsyncPath;
    bool m_pendingShowModalError = true;

    // Estado visual clínico (inversão, rotação, espelhamento)
    bool m_isInverted = false;
    int m_userRotationQuarterTurns = 0;
    bool m_userFlipHorizontal = false;

    // Estado de arraste com botão direito para WL/WW interativo
    bool m_isAdjustingWindow = false;
    QPoint m_lastMousePos;

    // Estado das ferramentas de medição (régua em mm e ROI)
    bool m_isMeasuringMode = false;
    bool m_isRoiMode = false;
    bool m_hasMeasureStart = false;
    QPointF m_measureStartScene;
    QGraphicsLineItem* m_previewLineItem = nullptr;
    QGraphicsRectItem* m_previewRectItem = nullptr;
    QList<QGraphicsItem*> m_measurementItems;
    QList<QGraphicsItem*> m_orientationItems;

    // Ações e Menus
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

    // Controles Principais
    QPushButton* m_openButton = nullptr;
    QLabel* m_infoHeaderLabel = nullptr;
    QLabel* m_errorBannerLabel = nullptr;

    // Controles de Window Level / Window Width e Presets Clínicos
    QWidget* m_windowControlBar = nullptr;
    QComboBox* m_windowPresetCombo = nullptr;
    QSlider* m_wcSlider = nullptr;
    QDoubleSpinBox* m_wcSpinBox = nullptr;
    QSlider* m_wwSlider = nullptr;
    QDoubleSpinBox* m_wwSpinBox = nullptr;
    QPushButton* m_resetWindowButton = nullptr;

    // Controles Multi-Frame / Cine
    QWidget* m_frameControlBar = nullptr;
    QPushButton* m_prevFrameButton = nullptr;
    QSlider* m_frameSlider = nullptr;
    QSpinBox* m_frameSpinBox = nullptr;
    QPushButton* m_nextFrameButton = nullptr;
    QLabel* m_frameTotalLabel = nullptr;
    QPushButton* m_cinePlayButton = nullptr;
    QTimer* m_cineTimer = nullptr;

    // Área de Visualização
    QGraphicsView* m_graphicsView = nullptr;
    QGraphicsScene* m_graphicsScene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;

    // Painel Lateral de Metadados DICOM
    QDockWidget* m_metadataDock = nullptr;
    QLineEdit* m_metadataFilterEdit = nullptr;
    QTableWidget* m_metadataTable = nullptr;
};

} // namespace dicom
