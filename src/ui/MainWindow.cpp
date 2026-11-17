#include "ui/MainWindow.h"
#include "core/DicomAnonymizer.h"
#include "ui/DicomViewport.h"
#include "ui/MetadataDockWidget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace dicom {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Visualizador DICOM Clínico - Qt"));
    resize(1240, 840);
    setMinimumSize(720, 520);
    setAcceptDrops(true);

    m_loadWatcher = new QFutureWatcher<DicomImageData>(this);
    connect(m_loadWatcher, &QFutureWatcher<DicomImageData>::finished, this, [this]() {
        QApplication::restoreOverrideCursor();
        const DicomImageData data = m_loadWatcher->result();
        const bool ok = applyLoadedData(m_pendingAsyncPath, data, m_pendingShowModalError);
        emit asyncLoadCompleted(ok);
    });

    m_cineTimer = new QTimer(this);
    m_cineTimer->setInterval(100);
    connect(m_cineTimer, &QTimer::timeout, this, [this]() {
        if (!hasLoadedImage() || m_currentData.frameCount <= 1) {
            m_cineTimer->stop();
            return;
        }
        setCurrentFrame((m_currentData.currentFrame + 1) % m_currentData.frameCount);
    });

    setupActionsAndToolbar();
    setupUi();

    statusBar()->showMessage(QStringLiteral("Pronto. Selecione ou arraste um arquivo DICOM (.dcm) para visualizar."));
}

MainWindow::~MainWindow() {
    if (m_cineTimer) {
        m_cineTimer->stop();
    }
    if (m_loadWatcher && m_loadWatcher->isRunning()) {
        m_loadWatcher->waitForFinished();
    }
}

void MainWindow::setupActionsAndToolbar() {
    m_openAction = new QAction(QStringLiteral("Abrir DICOM..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFileTriggered);

    m_exportImageAction = new QAction(QStringLiteral("Exportar Imagem (PNG/JPG)..."), this);
    m_exportImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_exportImageAction->setEnabled(false);
    connect(m_exportImageAction, &QAction::triggered, this, &MainWindow::onExportImageTriggered);

    m_exportAnonAction = new QAction(QStringLiteral("Exportar LGPD"), this);
    m_exportAnonAction->setToolTip(QStringLiteral("Exportar arquivo DICOM anonimizado sem dados pessoais (LGPD)"));
    m_exportAnonAction->setEnabled(false);
    connect(m_exportAnonAction, &QAction::triggered, this, &MainWindow::onExportAnonymizedDicomTriggered);

    m_fitAction = new QAction(QStringLiteral("Ajustar"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_fitAction->setEnabled(false);
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::onFitToWindow);

    m_actualSizeAction = new QAction(QStringLiteral("100%"), this);
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

    m_invertAction = new QAction(QStringLiteral("Inverter"), this);
    m_invertAction->setCheckable(true);
    m_invertAction->setEnabled(false);
    m_invertAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(m_invertAction, &QAction::toggled, this, &MainWindow::onInvertToggled);

    m_rotateAction = new QAction(QStringLiteral("Girar 90°"), this);
    m_rotateAction->setEnabled(false);
    m_rotateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_rotateAction, &QAction::triggered, this, &MainWindow::onRotate90Triggered);

    m_flipHAction = new QAction(QStringLiteral("Espelhar H"), this);
    m_flipHAction->setCheckable(true);
    m_flipHAction->setEnabled(false);
    connect(m_flipHAction, &QAction::triggered, this, &MainWindow::onFlipHorizontalTriggered);

    m_measureAction = new QAction(QStringLiteral("Régua (mm)"), this);
    m_measureAction->setCheckable(true);
    m_measureAction->setEnabled(false);
    connect(m_measureAction, &QAction::toggled, this, &MainWindow::onMeasureModeToggled);

    m_roiAction = new QAction(QStringLiteral("ROI (HU)"), this);
    m_roiAction->setCheckable(true);
    m_roiAction->setEnabled(false);
    connect(m_roiAction, &QAction::toggled, this, &MainWindow::onRoiModeToggled);

    m_clearMeasuresAction = new QAction(QStringLiteral("Limpar Medidas"), this);
    m_clearMeasuresAction->setEnabled(false);
    connect(m_clearMeasuresAction, &QAction::triggered, this, &MainWindow::onClearMeasurementsTriggered);

    m_toggleMetadataAction = new QAction(QStringLiteral("Metadados"), this);
    m_toggleMetadataAction->setCheckable(true);
    m_toggleMetadataAction->setChecked(true);
    m_toggleMetadataAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportImageAction);
    fileMenu->addAction(m_exportAnonAction);
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(QStringLiteral("Sair"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&Visualizar"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_invertAction);
    viewMenu->addAction(m_rotateAction);
    viewMenu->addAction(m_flipHAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_measureAction);
    viewMenu->addAction(m_roiAction);
    viewMenu->addAction(m_clearMeasuresAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleMetadataAction);

    QToolBar* toolBar = addToolBar(QStringLiteral("Ferramentas Clínicas"));
    toolBar->setMovable(false);
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_exportAnonAction);
    toolBar->addSeparator();
    toolBar->addAction(m_fitAction);
    toolBar->addAction(m_actualSizeAction);
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
    toolBar->addSeparator();
    toolBar->addAction(m_invertAction);
    toolBar->addAction(m_rotateAction);
    toolBar->addAction(m_flipHAction);
    toolBar->addSeparator();
    toolBar->addAction(m_measureAction);
    toolBar->addAction(m_roiAction);
    toolBar->addAction(m_clearMeasuresAction);
    toolBar->addSeparator();
    toolBar->addAction(m_toggleMetadataAction);
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto* topBarLayout = new QHBoxLayout();
    m_openButton = new QPushButton(QStringLiteral("Selecionar Arquivo DICOM (.dcm)..."), centralWidget);
    m_openButton->setMinimumHeight(34);
    m_openButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #1976d2; color: white; font-weight: bold; "
        "padding: 6px 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #1565c0; }"));
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::onOpenFileTriggered);

    m_infoHeaderLabel = new QLabel(
        QStringLiteral("Nenhum arquivo carregado. Clique em 'Selecionar Arquivo DICOM (.dcm)...' ou arraste um arquivo."),
        centralWidget);
    m_infoHeaderLabel->setStyleSheet(QStringLiteral("color: #e0e0e0; font-size: 13px; padding-left: 8px;"));
    m_infoHeaderLabel->setWordWrap(true);

    topBarLayout->addWidget(m_openButton, 0);
    topBarLayout->addWidget(m_infoHeaderLabel, 1);
    mainLayout->addLayout(topBarLayout);

    // Barra de Window Level / Window Width e Presets Clínicos
    m_windowControlBar = new QWidget(centralWidget);
    m_windowControlBar->setObjectName(QStringLiteral("windowControlBar"));
    auto* wlLayout = new QHBoxLayout(m_windowControlBar);
    wlLayout->setContentsMargins(6, 4, 6, 4);
    wlLayout->setSpacing(8);

    auto* presetLabel = new QLabel(QStringLiteral("Preset:"), m_windowControlBar);
    m_windowPresetCombo = new QComboBox(m_windowControlBar);
    m_windowPresetCombo->setObjectName(QStringLiteral("windowPresetCombo"));
    m_windowPresetCombo->addItems({
        QStringLiteral("Padrão DICOM (VOI LUT)"),
        QStringLiteral("CT Pulmão (-600 / 1500)"),
        QStringLiteral("CT Osso (300 / 1500)"),
        QStringLiteral("CT Abdômen / Tecido Mole (40 / 400)"),
        QStringLiteral("CT Cérebro (40 / 80)")
    });
    connect(m_windowPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_updatingWindowControls) applyClinicalWindowPreset(idx);
    });

    auto* wcLabel = new QLabel(QStringLiteral("WL:"), m_windowControlBar);
    m_wcSlider = new QSlider(Qt::Horizontal, m_windowControlBar);
    m_wcSlider->setObjectName(QStringLiteral("wcSlider"));
    m_wcSpinBox = new QDoubleSpinBox(m_windowControlBar);
    m_wcSpinBox->setObjectName(QStringLiteral("wcSpinBox"));
    m_wcSpinBox->setDecimals(1);
    m_wcSpinBox->setRange(-32768.0, 65535.0);

    auto* wwLabel = new QLabel(QStringLiteral("WW:"), m_windowControlBar);
    m_wwSlider = new QSlider(Qt::Horizontal, m_windowControlBar);
    m_wwSlider->setObjectName(QStringLiteral("wwSlider"));
    m_wwSpinBox = new QDoubleSpinBox(m_windowControlBar);
    m_wwSpinBox->setObjectName(QStringLiteral("wwSpinBox"));
    m_wwSpinBox->setDecimals(1);
    m_wwSpinBox->setRange(1.0, 65535.0);

    m_resetWindowButton = new QPushButton(QStringLiteral("Resetar WL/WW"), m_windowControlBar);
    m_resetWindowButton->setObjectName(QStringLiteral("resetWindowButton"));

    wlLayout->addWidget(presetLabel);
    wlLayout->addWidget(m_windowPresetCombo);
    wlLayout->addSpacing(8);
    wlLayout->addWidget(wcLabel);
    wlLayout->addWidget(m_wcSlider, 1);
    wlLayout->addWidget(m_wcSpinBox);
    wlLayout->addSpacing(8);
    wlLayout->addWidget(wwLabel);
    wlLayout->addWidget(m_wwSlider, 1);
    wlLayout->addWidget(m_wwSpinBox);
    wlLayout->addWidget(m_resetWindowButton);

    m_windowControlBar->setEnabled(false);
    mainLayout->addWidget(m_windowControlBar);

    connect(m_wcSlider, &QSlider::valueChanged, this, [this](int val) {
        if (!m_updatingWindowControls) setWindowLevelWidth(static_cast<double>(val), m_wwSpinBox->value());
    });
    connect(m_wcSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (!m_updatingWindowControls) setWindowLevelWidth(val, m_wwSpinBox->value());
    });
    connect(m_wwSlider, &QSlider::valueChanged, this, [this](int val) {
        if (!m_updatingWindowControls) setWindowLevelWidth(m_wcSpinBox->value(), static_cast<double>(val));
    });
    connect(m_wwSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (!m_updatingWindowControls) setWindowLevelWidth(m_wcSpinBox->value(), val);
    });
    connect(m_resetWindowButton, &QPushButton::clicked, this, &MainWindow::onResetWindowTriggered);

    // Barra Multi-Frame / Cine
    m_frameControlBar = new QWidget(centralWidget);
    m_frameControlBar->setObjectName(QStringLiteral("frameControlBar"));
    auto* frameLayout = new QHBoxLayout(m_frameControlBar);
    frameLayout->setContentsMargins(6, 2, 6, 2);
    frameLayout->setSpacing(8);

    auto* frameTitle = new QLabel(QStringLiteral("Navegação de Frames / Cortes:"), m_frameControlBar);
    m_prevFrameButton = new QPushButton(QStringLiteral("< Anterior"), m_frameControlBar);
    m_frameSlider = new QSlider(Qt::Horizontal, m_frameControlBar);
    m_frameSlider->setObjectName(QStringLiteral("frameSlider"));
    m_frameSpinBox = new QSpinBox(m_frameControlBar);
    m_frameSpinBox->setObjectName(QStringLiteral("frameSpinBox"));
    m_frameTotalLabel = new QLabel(QStringLiteral("/ 1"), m_frameControlBar);
    m_nextFrameButton = new QPushButton(QStringLiteral("Próximo >"), m_frameControlBar);
    m_cinePlayButton = new QPushButton(QStringLiteral("Play Cine"), m_frameControlBar);
    m_cinePlayButton->setCheckable(true);

    frameLayout->addWidget(frameTitle);
    frameLayout->addWidget(m_prevFrameButton);
    frameLayout->addWidget(m_frameSlider, 1);
    frameLayout->addWidget(m_frameSpinBox);
    frameLayout->addWidget(m_frameTotalLabel);
    frameLayout->addWidget(m_nextFrameButton);
    frameLayout->addWidget(m_cinePlayButton);

    m_frameControlBar->setVisible(false);
    mainLayout->addWidget(m_frameControlBar);

    connect(m_prevFrameButton, &QPushButton::clicked, this, [this]() {
        setCurrentFrame(std::max(0, m_currentData.currentFrame - 1));
    });
    connect(m_nextFrameButton, &QPushButton::clicked, this, [this]() {
        setCurrentFrame(std::min(m_currentData.frameCount - 1, m_currentData.currentFrame + 1));
    });
    connect(m_frameSlider, &QSlider::valueChanged, this, [this](int val) {
        if (!m_updatingFrameControls) setCurrentFrame(val - 1);
    });
    connect(m_frameSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (!m_updatingFrameControls) setCurrentFrame(val - 1);
    });
    connect(m_cinePlayButton, &QPushButton::toggled, this, &MainWindow::onCinePlayToggled);

    // Banner de erro para arquivos inválidos
    m_errorBannerLabel = new QLabel(centralWidget);
    m_errorBannerLabel->setObjectName(QStringLiteral("errorBannerLabel"));
    m_errorBannerLabel->setStyleSheet(QStringLiteral(
        "background-color: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; "
        "border-radius: 4px; padding: 8px 12px; font-weight: bold;"));
    m_errorBannerLabel->setWordWrap(true);
    m_errorBannerLabel->setVisible(false);
    mainLayout->addWidget(m_errorBannerLabel);

    // Viewport radiológico desacoplado
    m_viewport = new DicomViewport(centralWidget);
    connect(m_viewport, &DicomViewport::windowLevelAdjusted, this, &MainWindow::setWindowLevelWidth);
    connect(m_viewport, &DicomViewport::statusNotification, this, [this](const QString& msg) {
        statusBar()->showMessage(msg);
    });
    connect(m_viewport, &DicomViewport::pixelHovered, this, [this](int px, int py, double val) {
        statusBar()->showMessage(
            QStringLiteral("Pos: (%1, %2) | Valor Calibrado: %3 | WL: %4 | WW: %5")
                .arg(px)
                .arg(py)
                .arg(val, 0, 'f', 1)
                .arg(m_currentData.windowCenter, 0, 'f', 1)
                .arg(m_currentData.windowWidth, 0, 'f', 1));
    });
    mainLayout->addWidget(m_viewport, 1);
    setCentralWidget(centralWidget);

    // Painel lateral de metadados desacoplado
    m_metadataDock = new MetadataDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
    connect(m_toggleMetadataAction, &QAction::toggled, m_metadataDock, &QDockWidget::setVisible);
    connect(m_metadataDock, &QDockWidget::visibilityChanged, m_toggleMetadataAction, &QAction::setChecked);
}

void MainWindow::onOpenFileTriggered() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Selecionar Arquivo DICOM"),
        QString(),
        QStringLiteral("Arquivos DICOM (*.dcm *.dicom *.DCM);;Todos os Arquivos (*.*)"));

    if (!filePath.isEmpty()) {
        loadDicomFileAsync(filePath, true);
    }
}

bool MainWindow::loadDicomFile(const QString& filePath, bool showModalError) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const DicomImageData data = m_loader.loadFile(filePath, 0);
    QApplication::restoreOverrideCursor();
    return applyLoadedData(filePath, data, showModalError);
}

void MainWindow::loadDicomFileAsync(const QString& filePath, bool showModalError) {
    if (m_loadWatcher->isRunning()) {
        m_loadWatcher->waitForFinished();
    }
    m_pendingAsyncPath = filePath;
    m_pendingShowModalError = showModalError;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(QStringLiteral("Carregando e decodificando exame DICOM..."));

    auto future = QtConcurrent::run([filePath]() {
        DicomLoader asyncLoader;
        return asyncLoader.loadFile(filePath, 0);
    });
    m_loadWatcher->setFuture(future);
}

bool MainWindow::applyLoadedData(const QString& filePath, const DicomImageData& data, bool showModalError) {
    if (!data.isValid) {
        m_lastError = data.errorMessage;
        m_errorBannerLabel->setText(QStringLiteral("Erro ao carregar arquivo: %1").arg(data.errorMessage));
        m_errorBannerLabel->setVisible(true);
        statusBar()->showMessage(QStringLiteral("Falha ao carregar arquivo DICOM."));
        m_auditLogger.logEvent(QStringLiteral("OPEN_FAILED"), filePath, QStringLiteral("ERROR"), data.errorMessage);

        if (showModalError) {
            QMessageBox::critical(this, QStringLiteral("Arquivo DICOM Inválido"), data.errorMessage);
        }
        return false;
    }

    if (m_cineTimer->isActive()) {
        m_cineTimer->stop();
        m_cinePlayButton->setChecked(false);
    }

    m_lastError.clear();
    m_errorBannerLabel->clear();
    m_errorBannerLabel->setVisible(false);

    m_currentFilePath = filePath;
    m_currentData = data;
    m_invertAction->setChecked(false);
    m_flipHAction->setChecked(false);

    m_viewport->setImageData(m_currentData, true);

    m_exportImageAction->setEnabled(true);
    m_exportAnonAction->setEnabled(true);
    m_fitAction->setEnabled(true);
    m_actualSizeAction->setEnabled(true);
    m_zoomInAction->setEnabled(true);
    m_zoomOutAction->setEnabled(true);
    m_invertAction->setEnabled(true);
    m_rotateAction->setEnabled(true);
    m_flipHAction->setEnabled(true);
    m_measureAction->setEnabled(true);
    m_roiAction->setEnabled(true);
    m_clearMeasuresAction->setEnabled(true);

    updateWindowControls(m_currentData);
    updateFrameControls(m_currentData);
    m_metadataDock->setEntries(m_currentData.metadata);
    updateInfoPanel(filePath, m_currentData);

    m_auditLogger.logEvent(QStringLiteral("OPEN_SUCCESS"),
                           filePath,
                           QStringLiteral("OK"),
                           QStringLiteral("%1x%2 frames=%3 %4")
                               .arg(data.width)
                               .arg(data.height)
                               .arg(data.frameCount)
                               .arg(data.modality));
    return true;
}

void MainWindow::onExportImageTriggered() {
    if (!hasLoadedImage()) return;

    const QString outPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Exportar Imagem para Laudo/Prontuário"),
        QStringLiteral("exame_exportado.png"),
        QStringLiteral("Imagem PNG (*.png);;Imagem JPEG (*.jpg *.jpeg)"));
    if (outPath.isEmpty()) return;

    if (m_viewport->displayedPixmap().save(outPath)) {
        m_auditLogger.logEvent(QStringLiteral("EXPORT_IMAGE"), outPath, QStringLiteral("OK"));
        statusBar()->showMessage(QStringLiteral("Imagem exportada com sucesso para: %1").arg(outPath));
    } else {
        m_auditLogger.logEvent(QStringLiteral("EXPORT_IMAGE"), outPath, QStringLiteral("ERROR"));
        QMessageBox::warning(this, QStringLiteral("Erro ao Exportar"), QStringLiteral("Nao foi possivel salvar a imagem."));
    }
}

void MainWindow::onExportAnonymizedDicomTriggered() {
    if (!hasLoadedImage() || m_currentFilePath.isEmpty()) return;

    const QString outPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Exportar DICOM Anonimizado (LGPD)"),
        QStringLiteral("exame_anonimizado.dcm"),
        QStringLiteral("Arquivo DICOM (*.dcm)"));
    if (outPath.isEmpty()) return;

    QString err;
    if (DicomAnonymizer::anonymizeFile(m_currentFilePath, outPath, err)) {
        m_auditLogger.logEvent(QStringLiteral("ANONYMIZE_EXPORT"), outPath, QStringLiteral("OK"));
        statusBar()->showMessage(QStringLiteral("DICOM anonimizado (LGPD) salvo em: %1").arg(outPath));
        QMessageBox::information(this,
                                 QStringLiteral("Anonimização LGPD Concluída"),
                                 QStringLiteral("Arquivo DICOM salvo sem dados pessoais do paciente."));
    } else {
        m_auditLogger.logEvent(QStringLiteral("ANONYMIZE_EXPORT"), outPath, QStringLiteral("ERROR"), err);
        QMessageBox::critical(this, QStringLiteral("Erro na Anonimização"), err);
    }
}

void MainWindow::onInvertToggled(bool checked) {
    m_viewport->setInverted(checked);
}

void MainWindow::onRotate90Triggered() {
    m_viewport->rotate90Clockwise();
}

void MainWindow::onFlipHorizontalTriggered() {
    m_viewport->toggleFlipHorizontal();
}

void MainWindow::onMeasureModeToggled(bool checked) {
    if (checked && m_roiAction->isChecked()) {
        m_roiAction->setChecked(false);
    }
    m_viewport->setInteractionMode(checked ? DicomViewport::InteractionMode::MeasureDistance
                                           : DicomViewport::InteractionMode::Navigate);
    statusBar()->showMessage(checked
        ? QStringLiteral("Modo Régua ativo: clique no ponto inicial e depois no ponto final para medir em mm.")
        : QStringLiteral("Modo Navegação ativo."));
}

void MainWindow::onRoiModeToggled(bool checked) {
    if (checked && m_measureAction->isChecked()) {
        m_measureAction->setChecked(false);
    }
    m_viewport->setInteractionMode(checked ? DicomViewport::InteractionMode::MeasureRoi
                                           : DicomViewport::InteractionMode::Navigate);
    statusBar()->showMessage(checked
        ? QStringLiteral("Modo ROI ativo: clique em dois cantos opostos para calcular estatísticas (Média, DP, Área).")
        : QStringLiteral("Modo Navegação ativo."));
}

double MainWindow::measureDistanceMm(const QPointF& p1, const QPointF& p2) {
    return m_viewport->measureDistanceMm(p1, p2);
}

RoiStatistics MainWindow::measureRoiStats(const QRectF& sceneRect) {
    return m_viewport->measureRoiStats(sceneRect);
}

void MainWindow::onClearMeasurementsTriggered() {
    m_viewport->clearMeasurements();
}

void MainWindow::updateFrameControls(const DicomImageData& data) {
    m_updatingFrameControls = true;
    const bool multiFrame = data.isValid && data.frameCount > 1;
    m_frameControlBar->setVisible(multiFrame);
    if (multiFrame) {
        m_frameSlider->setRange(1, data.frameCount);
        m_frameSpinBox->setRange(1, data.frameCount);
        m_frameSlider->setValue(data.currentFrame + 1);
        m_frameSpinBox->setValue(data.currentFrame + 1);
        m_frameTotalLabel->setText(QStringLiteral("/ %1").arg(data.frameCount));
    }
    m_updatingFrameControls = false;
}

bool MainWindow::setCurrentFrame(int frameIndex) {
    if (!m_currentData.isValid || frameIndex < 0 || frameIndex >= m_currentData.frameCount) {
        return false;
    }
    if (frameIndex == m_currentData.currentFrame) {
        return true;
    }
    const DicomImageData updated = m_loader.loadFrame(m_currentData, frameIndex);
    if (!updated.isValid) {
        return false;
    }

    m_currentData = updated;
    m_viewport->updateImageDataKeepView(m_currentData);

    m_updatingFrameControls = true;
    m_frameSlider->setValue(frameIndex + 1);
    m_frameSpinBox->setValue(frameIndex + 1);
    m_updatingFrameControls = false;

    statusBar()->showMessage(
        QStringLiteral("Frame %1 de %2").arg(frameIndex + 1).arg(m_currentData.frameCount));
    return true;
}

void MainWindow::onCinePlayToggled(bool checked) {
    if (checked && m_currentData.frameCount > 1) {
        m_cinePlayButton->setText(QStringLiteral("Pausar Cine"));
        m_cineTimer->start();
    } else {
        m_cinePlayButton->setText(QStringLiteral("Play Cine"));
        m_cineTimer->stop();
    }
}

void MainWindow::applyClinicalWindowPreset(int presetIndex) {
    if (!m_currentData.isValid || !m_currentData.isMonochrome) return;
    switch (presetIndex) {
    case 0: resetWindowLevelWidth(); break;
    case 1: setWindowLevelWidth(-600.0, 1500.0); break;
    case 2: setWindowLevelWidth(300.0, 1500.0); break;
    case 3: setWindowLevelWidth(40.0, 400.0); break;
    case 4: setWindowLevelWidth(40.0, 80.0); break;
    default: break;
    }
}

void MainWindow::updateWindowControls(const DicomImageData& data) {
    m_updatingWindowControls = true;

    const bool enableWl = data.isValid && data.isMonochrome && data.hasWindow;
    m_windowControlBar->setEnabled(enableWl);

    if (enableWl) {
        const int minRange = static_cast<int>(std::floor(std::min({-1000.0, data.minPixelValue, data.windowCenter - data.windowWidth})));
        const int maxRange = static_cast<int>(std::ceil(std::max({3000.0, data.maxPixelValue, data.windowCenter + data.windowWidth})));
        const int maxWidth = std::max(1, static_cast<int>(std::ceil((maxRange - minRange) * 2.0)));

        m_windowPresetCombo->setCurrentIndex(0);
        m_wcSlider->setRange(minRange, maxRange);
        m_wcSpinBox->setRange(static_cast<double>(minRange), static_cast<double>(maxRange));
        m_wcSlider->setValue(static_cast<int>(std::lround(data.windowCenter)));
        m_wcSpinBox->setValue(data.windowCenter);

        m_wwSlider->setRange(1, maxWidth);
        m_wwSpinBox->setRange(1.0, static_cast<double>(maxWidth));
        m_wwSlider->setValue(std::max(1, static_cast<int>(std::lround(data.windowWidth))));
        m_wwSpinBox->setValue(std::max(1.0, data.windowWidth));
    }

    m_updatingWindowControls = false;
}

void MainWindow::setWindowLevelWidth(double center, double width) {
    if (!m_currentData.isValid || !m_currentData.isMonochrome) return;

    const DicomImageData updated = m_loader.applyWindowLevel(m_currentData, center, width);
    if (!updated.isValid) return;

    m_currentData = updated;
    m_viewport->updateImageDataKeepView(m_currentData);

    m_updatingWindowControls = true;
    m_wcSpinBox->setValue(m_currentData.windowCenter);
    m_wcSlider->setValue(static_cast<int>(std::lround(m_currentData.windowCenter)));
    m_wwSpinBox->setValue(m_currentData.windowWidth);
    m_wwSlider->setValue(std::max(1, static_cast<int>(std::lround(m_currentData.windowWidth))));
    m_updatingWindowControls = false;

    statusBar()->showMessage(
        QStringLiteral("Window Level: %1 | Window Width: %2")
            .arg(m_currentData.windowCenter, 0, 'f', 1)
            .arg(m_currentData.windowWidth, 0, 'f', 1));
}

void MainWindow::resetWindowLevelWidth() {
    onResetWindowTriggered();
}

void MainWindow::onResetWindowTriggered() {
    if (!m_currentData.isValid || !m_currentData.isMonochrome) return;

    const DicomImageData updated = m_loader.resetWindowLevel(m_currentData);
    if (!updated.isValid) return;

    m_currentData = updated;
    m_viewport->updateImageDataKeepView(m_currentData);
    updateWindowControls(m_currentData);

    statusBar()->showMessage(QStringLiteral("Janelamento padrão (VOI LUT / DICOM) restaurado."));
}

void MainWindow::updateInfoPanel(const QString& filePath, const DicomImageData& data) {
    const QFileInfo fi(filePath);

    QStringList details;
    details << QStringLiteral("<b>Arquivo:</b> %1").arg(fi.fileName().toHtmlEscaped());
    details << QStringLiteral("<b>Dimensões:</b> %1 x %2 px").arg(data.width).arg(data.height);
    details << QStringLiteral("<b>Escala:</b> %1 (%2 bits)").arg(data.photometricInterpretation).arg(data.bitsStored);

    if (data.frameCount > 1) {
        details << QStringLiteral("<b>Frames:</b> %1").arg(data.frameCount);
    }
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
    setWindowTitle(QStringLiteral("Visualizador DICOM Clínico - %1 (%2x%3)")
                       .arg(fi.fileName())
                       .arg(data.width)
                       .arg(data.height));
    statusBar()->showMessage(
        QStringLiteral("Imagem DICOM carregada com sucesso (%1 x %2 pixels, %3).")
            .arg(data.width)
            .arg(data.height)
            .arg(data.photometricInterpretation));
}

void MainWindow::onZoomIn() { m_viewport->zoomIn(); }
void MainWindow::onZoomOut() { m_viewport->zoomOut(); }
void MainWindow::onResetZoom() { m_viewport->resetZoom(); }
void MainWindow::onFitToWindow() { m_viewport->fitToWindow(); }

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        const QString filePath = urls.first().toLocalFile();
        event->acceptProposedAction();
        loadDicomFileAsync(filePath, true);
        return;
    }
    QMainWindow::dropEvent(event);
}

bool MainWindow::hasLoadedImage() const { return m_viewport && m_viewport->hasImage(); }
QSize MainWindow::displayedImageSize() const { return m_viewport ? m_viewport->displayedImageSize() : QSize(); }
QImage MainWindow::displayedImage() const { return m_viewport ? m_viewport->displayedImage() : QImage(); }
const QString& MainWindow::lastErrorMessage() const { return m_lastError; }
QString MainWindow::statusSummaryText() const { return m_infoHeaderLabel ? m_infoHeaderLabel->text() : QString(); }
bool MainWindow::isErrorBannerVisible() const { return m_errorBannerLabel && m_errorBannerLabel->isVisible(); }
double MainWindow::currentWindowCenter() const { return m_currentData.windowCenter; }
double MainWindow::currentWindowWidth() const { return m_currentData.windowWidth; }
int MainWindow::metadataRowCount() const { return m_metadataDock ? m_metadataDock->rowCount() : 0; }
bool MainWindow::isMetadataPanelVisible() const { return m_metadataDock && m_metadataDock->isVisible(); }
bool MainWindow::isInverted() const { return m_viewport && m_viewport->isInverted(); }
int MainWindow::measurementCount() const { return m_viewport ? m_viewport->measurementCount() : 0; }

} // namespace dicom
