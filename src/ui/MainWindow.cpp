#include "ui/MainWindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace dicom {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Visualizador DICOM - Qt"));
    resize(1024, 768);
    setMinimumSize(640, 480);

    setupActionsAndToolbar();
    setupUi();

    statusBar()->showMessage(QStringLiteral("Pronto. Selecione um arquivo DICOM (.dcm) para visualizar."));
}

void MainWindow::setupActionsAndToolbar() {
    m_openAction = new QAction(QStringLiteral("Abrir Arquivo DICOM..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(QStringLiteral("Selecionar um arquivo DICOM (.dcm) no disco"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFileTriggered);

    m_fitAction = new QAction(QStringLiteral("Ajustar a Janela"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_fitAction->setEnabled(false);
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::onFitToWindow);

    m_actualSizeAction = new QAction(QStringLiteral("Tamanho 100%"), this);
    m_actualSizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_actualSizeAction->setEnabled(false);
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::onResetZoom);

    m_zoomInAction = new QAction(QStringLiteral("Zoom +"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setEnabled(false);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);

    m_zoomOutAction = new QAction(QStringLiteral("Zoom -"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setEnabled(false);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);

    // Menu Arquivo
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(QStringLiteral("Sair"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    // Menu Visualizar
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&Visualizar"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);

    // Barra de Ferramentas Principal
    QToolBar* toolBar = addToolBar(QStringLiteral("Ferramentas"));
    toolBar->setMovable(false);
    toolBar->addAction(m_openAction);
    toolBar->addSeparator();
    toolBar->addAction(m_fitAction);
    toolBar->addAction(m_actualSizeAction);
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Barra superior com botão rápido de seleção e resumo da imagem
    auto* topBarLayout = new QHBoxLayout();
    m_openButton = new QPushButton(QStringLiteral("Selecionar Arquivo DICOM (.dcm)..."), centralWidget);
    m_openButton->setMinimumHeight(34);
    m_openButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #1976d2; color: white; font-weight: bold; "
        "padding: 6px 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #1565c0; }"));
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::onOpenFileTriggered);

    m_infoHeaderLabel = new QLabel(
        QStringLiteral("Nenhum arquivo carregado. Clique em 'Selecionar Arquivo DICOM (.dcm)...' para abrir um exame."),
        centralWidget);
    m_infoHeaderLabel->setStyleSheet(QStringLiteral("color: #e0e0e0; font-size: 13px; padding-left: 8px;"));
    m_infoHeaderLabel->setWordWrap(true);

    topBarLayout->addWidget(m_openButton, 0);
    topBarLayout->addWidget(m_infoHeaderLabel, 1);
    mainLayout->addLayout(topBarLayout);

    // Banner de erro para arquivos inválidos (visível quando ocorre falha de leitura)
    m_errorBannerLabel = new QLabel(centralWidget);
    m_errorBannerLabel->setObjectName(QStringLiteral("errorBannerLabel"));
    m_errorBannerLabel->setStyleSheet(QStringLiteral(
        "background-color: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; "
        "border-radius: 4px; padding: 8px 12px; font-weight: bold;"));
    m_errorBannerLabel->setWordWrap(true);
    m_errorBannerLabel->setVisible(false);
    mainLayout->addWidget(m_errorBannerLabel);

    // Área de visualização radiológica (QGraphicsView + QGraphicsScene)
    m_graphicsScene = new QGraphicsScene(this);
    m_graphicsScene->setBackgroundBrush(QColor(10, 10, 10));

    m_graphicsView = new QGraphicsView(m_graphicsScene, centralWidget);
    m_graphicsView->setObjectName(QStringLiteral("dicomGraphicsView"));
    m_graphicsView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_graphicsView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_graphicsView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_graphicsView->setAlignment(Qt::AlignCenter);
    m_graphicsView->setStyleSheet(QStringLiteral("border: 1px solid #2c2c2c; background-color: #0a0a0a;"));
    m_graphicsView->viewport()->installEventFilter(this);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_graphicsScene->addItem(m_pixmapItem);

    mainLayout->addWidget(m_graphicsView, 1);
    setCentralWidget(centralWidget);
}

void MainWindow::onOpenFileTriggered() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Selecionar Arquivo DICOM"),
        QString(),
        QStringLiteral("Arquivos DICOM (*.dcm *.dicom *.DCM);;Todos os Arquivos (*.*)"));

    if (!filePath.isEmpty()) {
        loadDicomFile(filePath, true);
    }
}

bool MainWindow::loadDicomFile(const QString& filePath, bool showModalError) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const DicomImageData data = m_loader.loadFile(filePath);
    QApplication::restoreOverrideCursor();

    if (!data.isValid) {
        m_lastError = data.errorMessage;
        m_errorBannerLabel->setText(QStringLiteral("Erro ao carregar arquivo: %1").arg(data.errorMessage));
        m_errorBannerLabel->setVisible(true);
        statusBar()->showMessage(QStringLiteral("Falha ao carregar arquivo DICOM."));

        if (showModalError) {
            QMessageBox::critical(this,
                                  QStringLiteral("Arquivo DICOM Inválido"),
                                  data.errorMessage);
        }
        return false;
    }

    // Sucesso: limpa erros anteriores e atualiza a visualização
    m_lastError.clear();
    m_errorBannerLabel->clear();
    m_errorBannerLabel->setVisible(false);
    m_currentData = data;

    const QPixmap pixmap = QPixmap::fromImage(data.image);
    m_pixmapItem->setPixmap(pixmap);
    m_graphicsScene->setSceneRect(pixmap.rect());

    m_fitAction->setEnabled(true);
    m_actualSizeAction->setEnabled(true);
    m_zoomInAction->setEnabled(true);
    m_zoomOutAction->setEnabled(true);

    updateInfoPanel(filePath, data);
    onFitToWindow();
    return true;
}

void MainWindow::updateInfoPanel(const QString& filePath, const DicomImageData& data) {
    const QFileInfo fi(filePath);

    QStringList details;
    details << QStringLiteral("<b>Arquivo:</b> %1").arg(fi.fileName().toHtmlEscaped());
    details << QStringLiteral("<b>Dimensões:</b> %1 x %2 px").arg(data.width).arg(data.height);
    details << QStringLiteral("<b>Escala:</b> %1 (%2 bits)").arg(data.photometricInterpretation).arg(data.bitsStored);

    if (!data.modality.isEmpty()) {
        details << QStringLiteral("<b>Modalidade:</b> %1").arg(data.modality);
    }
    if (!data.imageLaterality.isEmpty() || !data.viewPosition.isEmpty()) {
        details << QStringLiteral("<b>Projeção:</b> %1 %2").arg(data.imageLaterality, data.viewPosition).trimmed();
    }
    if (!data.patientOrientation.isEmpty()) {
        details << QStringLiteral("<b>Orientação:</b> %1").arg(data.patientOrientation);
    }

    m_infoHeaderLabel->setText(details.join(QStringLiteral(" &nbsp;|&nbsp; ")));
    setWindowTitle(QStringLiteral("Visualizador DICOM - %1 (%2x%3)")
                       .arg(fi.fileName())
                       .arg(data.width)
                       .arg(data.height));
    statusBar()->showMessage(
        QStringLiteral("Imagem DICOM carregada com sucesso (%1 x %2 pixels, %3).")
            .arg(data.width)
            .arg(data.height)
            .arg(data.photometricInterpretation));
}

void MainWindow::onZoomIn() {
    m_autoFitEnabled = false;
    applyZoom(1.25);
}

void MainWindow::onZoomOut() {
    m_autoFitEnabled = false;
    applyZoom(0.8);
}

void MainWindow::onResetZoom() {
    if (!hasLoadedImage()) {
        return;
    }
    m_autoFitEnabled = false;
    m_graphicsView->resetTransform();
}

void MainWindow::onFitToWindow() {
    if (!hasLoadedImage()) {
        return;
    }
    m_autoFitEnabled = true;
    m_graphicsView->resetTransform();
    m_graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

void MainWindow::applyZoom(double factor) {
    if (!hasLoadedImage()) {
        return;
    }
    m_graphicsView->scale(factor, factor);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_autoFitEnabled && hasLoadedImage()) {
        m_graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_graphicsView->viewport() && event->type() == QEvent::Wheel && hasLoadedImage()) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->angleDelta().y() != 0) {
            m_autoFitEnabled = false;
            const double factor = (wheelEvent->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
            applyZoom(factor);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::hasLoadedImage() const {
    return m_currentData.isValid && m_pixmapItem && !m_pixmapItem->pixmap().isNull();
}

QSize MainWindow::displayedImageSize() const {
    if (!hasLoadedImage()) {
        return QSize();
    }
    return m_pixmapItem->pixmap().size();
}

QImage MainWindow::displayedImage() const {
    if (!hasLoadedImage()) {
        return QImage();
    }
    return m_pixmapItem->pixmap().toImage();
}

QString MainWindow::lastErrorMessage() const {
    return m_lastError;
}

QString MainWindow::statusSummaryText() const {
    return m_infoHeaderLabel ? m_infoHeaderLabel->text() : QString();
}

bool MainWindow::isErrorBannerVisible() const {
    return m_errorBannerLabel && m_errorBannerLabel->isVisible();
}

} // namespace dicom
