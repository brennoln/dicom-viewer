#pragma once

#include "core/AuditLogger.h"
#include "core/DicomImageData.h"
#include "core/DicomLoader.h"

#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>

namespace dicom {

/**
 * @brief Janela principal da aplicação Qt para clínica médica: visualização DICOM,
 * ajuste de WL/WW, medições em mm, inspeção de pixels, anonimização LGPD e auditoria.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    /**
     * @brief Carrega um arquivo DICOM diretamente pelo caminho e atualiza a interface.
     * @param filePath Caminho para o arquivo .dcm
     * @param showModalError Se verdadeiro, exibe QMessageBox::critical em caso de erro
     * @return true se o arquivo foi carregado e exibido com sucesso
     */
    bool loadDicomFile(const QString& filePath, bool showModalError = true);

    /**
     * @brief Aplica novos valores de Window Level / Window Width na imagem exibida.
     */
    void setWindowLevelWidth(double center, double width);

    /**
     * @brief Restaura os parâmetros padrão (VOI LUT / Window original) da imagem.
     */
    void resetWindowLevelWidth();

    /**
     * @brief Adiciona uma medição de distância entre dois pontos da cena e retorna o valor em mm.
     */
    double measureDistanceMm(const QPointF& p1, const QPointF& p2);

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
    void onClearMeasurementsTriggered();
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
    void applyZoom(double factor);
    void updateInfoPanel(const QString& filePath, const DicomImageData& data);
    void updateWindowControls(const DicomImageData& data);
    void updateMetadataTable(const QVector<DicomMetadataEntry>& entries);
    void refreshDisplayedPixmap();
    void clearMeasurements();

    DicomLoader m_loader;
    AuditLogger m_auditLogger;
    DicomImageData m_currentData;
    QString m_currentFilePath;
    QString m_lastError;
    bool m_autoFitEnabled = true;
    bool m_updatingWindowControls = false;

    // Estado visual clínico (inversão, rotação, espelhamento)
    bool m_isInverted = false;
    int m_userRotationQuarterTurns = 0;
    bool m_userFlipHorizontal = false;

    // Estado de arraste com botão direito para WL/WW interativo
    bool m_isAdjustingWindow = false;
    QPoint m_lastMousePos;

    // Estado da ferramenta de medição (régua em mm)
    bool m_isMeasuringMode = false;
    bool m_hasMeasureStart = false;
    QPointF m_measureStartScene;
    QGraphicsLineItem* m_previewLineItem = nullptr;
    QList<QGraphicsItem*> m_measurementItems;

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
    QAction* m_clearMeasuresAction = nullptr;
    QAction* m_toggleMetadataAction = nullptr;

    // Controles Principais
    QPushButton* m_openButton = nullptr;
    QLabel* m_infoHeaderLabel = nullptr;
    QLabel* m_errorBannerLabel = nullptr;

    // Controles de Window Level / Window Width
    QWidget* m_windowControlBar = nullptr;
    QSlider* m_wcSlider = nullptr;
    QDoubleSpinBox* m_wcSpinBox = nullptr;
    QSlider* m_wwSlider = nullptr;
    QDoubleSpinBox* m_wwSpinBox = nullptr;
    QPushButton* m_resetWindowButton = nullptr;

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
