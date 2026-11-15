#pragma once

#include "core/DicomImageData.h"
#include "core/DicomLoader.h"

#include <QAction>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

namespace dicom {

/**
 * @brief Janela principal da aplicação Qt para seleção e visualização de imagens DICOM.
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

    // Métodos de inspeção para testes automatizados de interface (QtTest)
    bool hasLoadedImage() const;
    QSize displayedImageSize() const;
    QImage displayedImage() const;
    QString lastErrorMessage() const;
    QString statusSummaryText() const;
    bool isErrorBannerVisible() const;

public slots:
    void onOpenFileTriggered();
    void onZoomIn();
    void onZoomOut();
    void onResetZoom();
    void onFitToWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupActionsAndToolbar();
    void applyZoom(double factor);
    void updateInfoPanel(const QString& filePath, const DicomImageData& data);

    DicomLoader m_loader;
    DicomImageData m_currentData;
    QString m_lastError;
    bool m_autoFitEnabled = true;

    // Componentes da Interface Gráfica
    QAction* m_openAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;

    QPushButton* m_openButton = nullptr;
    QLabel* m_infoHeaderLabel = nullptr;
    QLabel* m_errorBannerLabel = nullptr;

    QGraphicsView* m_graphicsView = nullptr;
    QGraphicsScene* m_graphicsScene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
};

} // namespace dicom
