#include "ui/MainWindow.h"
#include "core/DicomAnonymizer.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

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
    m_cineTimer->setInterval(100); // 10 FPS padrão para playback clínico
    connect(m_cineTimer, &QTimer::timeout, this, [this]() {
        if (!hasLoadedImage() || m_currentData.frameCount <= 1) {
            m_cineTimer->stop();
            return;
        }
        const int nextFrame = (m_currentData.currentFrame + 1) % m_currentData.frameCount;
        setCurrentFrame(nextFrame);
    });

    setupActionsAndToolbar();
    setupUi();
    setupMetadataDock();

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
    m_measureAction->setToolTip(QStringLiteral("Clique em dois pontos na imagem para medir distância em milímetros"));
    connect(m_measureAction, &QAction::toggled, this, &MainWindow::onMeasureModeToggled);

    m_roiAction = new QAction(QStringLiteral("ROI (HU)"), this);
    m_roiAction->setCheckable(true);
    m_roiAction->setEnabled(false);
    m_roiAction->setToolTip(QStringLiteral("Clique em dois cantos para calcular Média, DP, Min/Max e Área (mm²)"));
    connect(m_roiAction, &QAction::toggled, this, &MainWindow::onRoiModeToggled);

    m_clearMeasuresAction = new QAction(QStringLiteral("Limpar Medidas"), this);
    m_clearMeasuresAction->setEnabled(false);
    connect(m_clearMeasuresAction, &QAction::triggered, this, &MainWindow::onClearMeasurementsTriggered);

    m_toggleMetadataAction = new QAction(QStringLiteral("Metadados"), this);
    m_toggleMetadataAction->setCheckable(true);
    m_toggleMetadataAction->setChecked(true);
    m_toggleMetadataAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));

    // Menu Arquivo
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportImageAction);
    fileMenu->addAction(m_exportAnonAction);
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(QStringLiteral("Sair"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    // Menu Visualizar / Ferramentas Clínicas
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

    // Barra de Ferramentas Principal
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
        QStringLiteral("Nenhum arquivo carregado. Clique em 'Selecionar Arquivo DICOM (.dcm)...' ou arraste um arquivo."),
        centralWidget);
    m_infoHeaderLabel->setStyleSheet(QStringLiteral("color: #e0e0e0; font-size: 13px; padding-left: 8px;"));
    m_infoHeaderLabel->setWordWrap(true);

    topBarLayout->addWidget(m_openButton, 0);
    topBarLayout->addWidget(m_infoHeaderLabel, 1);
    mainLayout->addLayout(topBarLayout);

    // Barra de controle de Window Level / Window Width e Presets Clínicos
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
        if (m_updatingWindowControls) return;
        applyClinicalWindowPreset(idx);
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
        if (m_updatingWindowControls) return;
        setWindowLevelWidth(static_cast<double>(val), m_wwSpinBox->value());
    });
    connect(m_wcSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (m_updatingWindowControls) return;
        setWindowLevelWidth(val, m_wwSpinBox->value());
    });
    connect(m_wwSlider, &QSlider::valueChanged, this, [this](int val) {
        if (m_updatingWindowControls) return;
        setWindowLevelWidth(m_wcSpinBox->value(), static_cast<double>(val));
    });
    connect(m_wwSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (m_updatingWindowControls) return;
        setWindowLevelWidth(m_wcSpinBox->value(), val);
    });
    connect(m_resetWindowButton, &QPushButton::clicked, this, &MainWindow::onResetWindowTriggered);

    // Barra de navegação Multi-Frame / Cine (exibida apenas quando frameCount > 1)
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
        if (m_updatingFrameControls) return;
        setCurrentFrame(val - 1);
    });
    connect(m_frameSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_updatingFrameControls) return;
        setCurrentFrame(val - 1);
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
    m_graphicsView->setMouseTracking(true);
    m_graphicsView->viewport()->setMouseTracking(true);
    m_graphicsView->viewport()->installEventFilter(this);
    m_graphicsView->setToolTip(QStringLiteral("Roda: Zoom | Esq: Mover | Dir + arraste: WL/WW | Régua/ROI: medir"));

    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_graphicsScene->addItem(m_pixmapItem);

    mainLayout->addWidget(m_graphicsView, 1);
    setCentralWidget(centralWidget);
}

void MainWindow::setupMetadataDock() {
    m_metadataDock = new QDockWidget(QStringLiteral("Metadados DICOM"), this);
    m_metadataDock->setObjectName(QStringLiteral("metadataDock"));
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_metadataDock->setMinimumWidth(290);

    auto* dockContainer = new QWidget(m_metadataDock);
    auto* dockLayout = new QVBoxLayout(dockContainer);
    dockLayout->setContentsMargins(6, 6, 6, 6);
    dockLayout->setSpacing(6);

    m_metadataFilterEdit = new QLineEdit(dockContainer);
    m_metadataFilterEdit->setObjectName(QStringLiteral("metadataFilterEdit"));
    m_metadataFilterEdit->setPlaceholderText(QStringLiteral("Filtrar tags ou valores..."));
    m_metadataFilterEdit->setClearButtonEnabled(true);
    connect(m_metadataFilterEdit, &QLineEdit::textChanged, this, &MainWindow::onMetadataFilterChanged);

    m_metadataTable = new QTableWidget(0, 3, dockContainer);
    m_metadataTable->setObjectName(QStringLiteral("metadataTable"));
    m_metadataTable->setHorizontalHeaderLabels({QStringLiteral("Tag"), QStringLiteral("Atributo"), QStringLiteral("Valor")});
    m_metadataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_metadataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_metadataTable->verticalHeader()->setVisible(false);
    m_metadataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_metadataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_metadataTable->setAlternatingRowColors(true);

    dockLayout->addWidget(m_metadataFilterEdit);
    dockLayout->addWidget(m_metadataTable, 1);
    m_metadataDock->setWidget(dockContainer);

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
            QMessageBox::critical(this,
                                  QStringLiteral("Arquivo DICOM Inválido"),
                                  data.errorMessage);
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
    clearMeasurements();

    m_currentFilePath = filePath;
    m_currentData = data;
    m_isInverted = false;
    m_userRotationQuarterTurns = 0;
    m_userFlipHorizontal = false;
    m_invertAction->setChecked(false);
    m_flipHAction->setChecked(false);

    refreshDisplayedPixmap();
    updateOrientationOverlay();

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
    updateMetadataTable(m_currentData.metadata);
    updateInfoPanel(filePath, m_currentData);
    onFitToWindow();

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
    if (!hasLoadedImage()) {
        return;
    }
    const QString outPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Exportar Imagem para Laudo/Prontuário"),
        QStringLiteral("exame_exportado.png"),
        QStringLiteral("Imagem PNG (*.png);;Imagem JPEG (*.jpg *.jpeg)"));
    if (outPath.isEmpty()) {
        return;
    }

    if (m_pixmapItem->pixmap().save(outPath)) {
        m_auditLogger.logEvent(QStringLiteral("EXPORT_IMAGE"), outPath, QStringLiteral("OK"));
        statusBar()->showMessage(QStringLiteral("Imagem exportada com sucesso para: %1").arg(outPath));
    } else {
        m_auditLogger.logEvent(QStringLiteral("EXPORT_IMAGE"), outPath, QStringLiteral("ERROR"));
        QMessageBox::warning(this, QStringLiteral("Erro ao Exportar"), QStringLiteral("Nao foi possivel salvar a imagem."));
    }
}

void MainWindow::onExportAnonymizedDicomTriggered() {
    if (!hasLoadedImage() || m_currentFilePath.isEmpty()) {
        return;
    }
    const QString outPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Exportar DICOM Anonimizado (LGPD)"),
        QStringLiteral("exame_anonimizado.dcm"),
        QStringLiteral("Arquivo DICOM (*.dcm)"));
    if (outPath.isEmpty()) {
        return;
    }

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

void MainWindow::refreshDisplayedPixmap() {
    if (!m_currentData.isValid) {
        return;
    }

    QImage img = m_currentData.image;
    if (m_isInverted) {
        img.invertPixels(QImage::InvertRgb);
    }
    if (m_userFlipHorizontal) {
        QTransform flip;
        flip.scale(-1.0, 1.0);
        img = img.transformed(flip);
    }
    if (m_userRotationQuarterTurns != 0) {
        QTransform rot;
        rot.rotate(static_cast<qreal>(m_userRotationQuarterTurns * 90));
        img = img.transformed(rot, Qt::SmoothTransformation);
    }

    const QPixmap pixmap = QPixmap::fromImage(img);
    m_pixmapItem->setPixmap(pixmap);
    m_graphicsScene->setSceneRect(pixmap.rect());
}

void MainWindow::updateOrientationOverlay() {
    for (QGraphicsItem* item : m_orientationItems) {
        m_graphicsScene->removeItem(item);
        delete item;
    }
    m_orientationItems.clear();

    if (!m_currentData.isValid || m_currentData.patientOrientation.isEmpty()) {
        return;
    }

    const QStringList parts = m_currentData.patientOrientation.split(QLatin1Char('\\'));
    if (parts.size() < 2) {
        return;
    }

    // PatientOrientation: parts[0] = direção para direita (fim da linha); parts[1] = direção para baixo (fim da coluna)
    QString rightLabel = parts[0].trimmed();
    QString leftLabel = oppositeAnatomyCode(rightLabel);
    QString bottomLabel = parts[1].trimmed();
    QString topLabel = oppositeAnatomyCode(bottomLabel);

    if (m_userFlipHorizontal) {
        std::swap(leftLabel, rightLabel);
    }
    for (int r = 0; r < m_userRotationQuarterTurns; ++r) {
        const QString prevTop = topLabel;
        topLabel = leftLabel;
        leftLabel = bottomLabel;
        bottomLabel = rightLabel;
        rightLabel = prevTop;
    }

    const QRectF rect = m_graphicsScene->sceneRect();
    if (rect.isEmpty()) {
        return;
    }

    auto addMarker = [this](const QString& text, const QPointF& pos) {
        if (text.isEmpty()) return;
        auto* item = m_graphicsScene->addSimpleText(text);
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

void MainWindow::onInvertToggled(bool checked) {
    m_isInverted = checked;
    refreshDisplayedPixmap();
}

void MainWindow::onRotate90Triggered() {
    if (!hasLoadedImage()) {
        return;
    }
    m_userRotationQuarterTurns = (m_userRotationQuarterTurns + 1) % 4;
    refreshDisplayedPixmap();
    updateOrientationOverlay();
    if (m_autoFitEnabled) {
        onFitToWindow();
    }
}

void MainWindow::onFlipHorizontalTriggered() {
    if (!hasLoadedImage()) {
        return;
    }
    m_userFlipHorizontal = !m_userFlipHorizontal;
    refreshDisplayedPixmap();
    updateOrientationOverlay();
}

void MainWindow::onMeasureModeToggled(bool checked) {
    m_isMeasuringMode = checked;
    if (checked && m_roiAction->isChecked()) {
        m_roiAction->setChecked(false);
    }
    m_hasMeasureStart = false;
    if (m_previewLineItem) {
        m_graphicsScene->removeItem(m_previewLineItem);
        delete m_previewLineItem;
        m_previewLineItem = nullptr;
    }
    m_graphicsView->setDragMode((m_isMeasuringMode || m_isRoiMode) ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    m_graphicsView->viewport()->setCursor((m_isMeasuringMode || m_isRoiMode) ? Qt::CrossCursor : Qt::ArrowCursor);
    statusBar()->showMessage(checked
        ? QStringLiteral("Modo Régua ativo: clique no ponto inicial e depois no ponto final para medir em mm.")
        : QStringLiteral("Modo Navegação ativo."));
}

void MainWindow::onRoiModeToggled(bool checked) {
    m_isRoiMode = checked;
    if (checked && m_measureAction->isChecked()) {
        m_measureAction->setChecked(false);
    }
    m_hasMeasureStart = false;
    if (m_previewRectItem) {
        m_graphicsScene->removeItem(m_previewRectItem);
        delete m_previewRectItem;
        m_previewRectItem = nullptr;
    }
    m_graphicsView->setDragMode((m_isMeasuringMode || m_isRoiMode) ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    m_graphicsView->viewport()->setCursor((m_isMeasuringMode || m_isRoiMode) ? Qt::CrossCursor : Qt::ArrowCursor);
    statusBar()->showMessage(checked
        ? QStringLiteral("Modo ROI ativo: clique em dois cantos opostos para calcular estatísticas (Média, DP, Área).")
        : QStringLiteral("Modo Navegação ativo."));
}

QPoint MainWindow::mapScenePointToOriginalPixel(const QPointF& scenePt) const {
    if (!m_currentData.isValid || m_currentData.width <= 0 || m_currentData.height <= 0) {
        return QPoint(-1, -1);
    }

    const int w0 = m_currentData.width;
    const int h0 = m_currentData.height;
    const int dx = static_cast<int>(std::floor(scenePt.x()));
    const int dy = static_cast<int>(std::floor(scenePt.y()));

    int px = dx;
    int py = dy;
    switch (m_userRotationQuarterTurns % 4) {
    case 1: // 90° horário
        px = dy;
        py = h0 - 1 - dx;
        break;
    case 2: // 180°
        px = w0 - 1 - dx;
        py = h0 - 1 - dy;
        break;
    case 3: // 270° horário
        px = w0 - 1 - dy;
        py = dx;
        break;
    default: // 0°
        break;
    }

    if (m_userFlipHorizontal) {
        px = w0 - 1 - px;
    }

    return QPoint(px, py);
}

double MainWindow::measureDistanceMm(const QPointF& p1, const QPointF& p2) {
    const bool swappedAxes = (m_userRotationQuarterTurns % 2) != 0;
    const double spacingX = swappedAxes ? m_currentData.pixelSpacingY : m_currentData.pixelSpacingX;
    const double spacingY = swappedAxes ? m_currentData.pixelSpacingX : m_currentData.pixelSpacingY;
    const double dxMm = (p2.x() - p1.x()) * spacingX;
    const double dyMm = (p2.y() - p1.y()) * spacingY;
    const double distMm = std::hypot(dxMm, dyMm);

    QPen pen(QColor(0, 230, 118), 2.0);
    pen.setCosmetic(true);
    auto* lineItem = m_graphicsScene->addLine(QLineF(p1, p2), pen);
    lineItem->setZValue(10.0);

    auto* labelItem = m_graphicsScene->addSimpleText(QStringLiteral("%1 mm").arg(distMm, 0, 'f', 1));
    labelItem->setBrush(QColor(0, 230, 118));
    labelItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    labelItem->setPos((p1 + p2) * 0.5);
    labelItem->setZValue(11.0);

    m_measurementItems.append(lineItem);
    m_measurementItems.append(labelItem);
    return distMm;
}

RoiStatistics MainWindow::measureRoiStats(const QRectF& sceneRect) {
    const QRectF normRect = sceneRect.normalized();
    const QPoint origA = mapScenePointToOriginalPixel(normRect.topLeft());
    const QPoint origB = mapScenePointToOriginalPixel(
        QPointF(normRect.right() - 1.0, normRect.bottom() - 1.0));
    const QRect pixelRect = QRect(origA, origB).normalized();
    const RoiStatistics stats = DicomLoader::computeRoiStatistics(m_currentData, pixelRect);
    if (!stats.valid) {
        return stats;
    }

    QPen pen(QColor(255, 213, 79), 2.0);
    pen.setCosmetic(true);
    auto* rectItem = m_graphicsScene->addRect(normRect, pen);
    rectItem->setZValue(10.0);

    const QString text = QStringLiteral("Média: %1 | DP: %2\nMin/Max: %3 / %4\nÁrea: %5 mm²")
                             .arg(stats.mean, 0, 'f', 1)
                             .arg(stats.stdDev, 0, 'f', 1)
                             .arg(stats.minVal, 0, 'f', 0)
                             .arg(stats.maxVal, 0, 'f', 0)
                             .arg(stats.areaMm2, 0, 'f', 1);
    auto* labelItem = m_graphicsScene->addSimpleText(text);
    labelItem->setBrush(QColor(255, 213, 79));
    labelItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    labelItem->setPos(normRect.topLeft());
    labelItem->setZValue(11.0);

    m_measurementItems.append(rectItem);
    m_measurementItems.append(labelItem);
    return stats;
}

void MainWindow::onClearMeasurementsTriggered() {
    clearMeasurements();
}

void MainWindow::clearMeasurements() {
    if (m_previewLineItem) {
        m_graphicsScene->removeItem(m_previewLineItem);
        delete m_previewLineItem;
        m_previewLineItem = nullptr;
    }
    if (m_previewRectItem) {
        m_graphicsScene->removeItem(m_previewRectItem);
        delete m_previewRectItem;
        m_previewRectItem = nullptr;
    }
    for (QGraphicsItem* item : m_measurementItems) {
        m_graphicsScene->removeItem(item);
        delete item;
    }
    m_measurementItems.clear();
    m_hasMeasureStart = false;
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
    refreshDisplayedPixmap();

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
    if (!m_currentData.isValid || !m_currentData.isMonochrome) {
        return;
    }
    switch (presetIndex) {
    case 0:
        resetWindowLevelWidth();
        break;
    case 1: // CT Pulmão
        setWindowLevelWidth(-600.0, 1500.0);
        break;
    case 2: // CT Osso
        setWindowLevelWidth(300.0, 1500.0);
        break;
    case 3: // CT Abdômen / Partes Moles
        setWindowLevelWidth(40.0, 400.0);
        break;
    case 4: // CT Cérebro
        setWindowLevelWidth(40.0, 80.0);
        break;
    default:
        break;
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

void MainWindow::updateMetadataTable(const QVector<DicomMetadataEntry>& entries) {
    m_metadataTable->setRowCount(0);
    m_metadataTable->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const auto& entry = entries.at(i);
        m_metadataTable->setItem(i, 0, new QTableWidgetItem(entry.tag));
        m_metadataTable->setItem(i, 1, new QTableWidgetItem(entry.name));
        m_metadataTable->setItem(i, 2, new QTableWidgetItem(entry.value));
    }

    onMetadataFilterChanged(m_metadataFilterEdit->text());
}

void MainWindow::onMetadataFilterChanged(const QString& filterText) {
    const QString needle = filterText.trimmed();
    for (int row = 0; row < m_metadataTable->rowCount(); ++row) {
        if (needle.isEmpty()) {
            m_metadataTable->setRowHidden(row, false);
            continue;
        }
        bool match = false;
        for (int col = 0; col < m_metadataTable->columnCount(); ++col) {
            if (auto* item = m_metadataTable->item(row, col)) {
                if (item->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_metadataTable->setRowHidden(row, !match);
    }
}

void MainWindow::setWindowLevelWidth(double center, double width) {
    if (!m_currentData.isValid || !m_currentData.isMonochrome) {
        return;
    }
    const DicomImageData updated = m_loader.applyWindowLevel(m_currentData, center, width);
    if (!updated.isValid) {
        return;
    }

    m_currentData = updated;
    refreshDisplayedPixmap();

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
    if (!m_currentData.isValid || !m_currentData.isMonochrome) {
        return;
    }
    const DicomImageData updated = m_loader.resetWindowLevel(m_currentData);
    if (!updated.isValid) {
        return;
    }
    m_currentData = updated;
    refreshDisplayedPixmap();
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
    if (watched == m_graphicsView->viewport() && hasLoadedImage()) {
        if (event->type() == QEvent::Wheel) {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->angleDelta().y() != 0) {
                if (m_currentData.frameCount > 1 && (wheelEvent->modifiers() & Qt::ControlModifier) == 0 &&
                    (m_isMeasuringMode || m_isRoiMode)) {
                    // Em ferramentas ativas, wheel ainda faz zoom
                }
                m_autoFitEnabled = false;
                const double factor = (wheelEvent->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
                applyZoom(factor);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_isMeasuringMode) {
                const QPointF scenePos = m_graphicsView->mapToScene(mouseEvent->pos());
                if (!m_hasMeasureStart) {
                    m_measureStartScene = scenePos;
                    m_hasMeasureStart = true;
                    QPen previewPen(QColor(255, 235, 59), 1.5, Qt::DashLine);
                    previewPen.setCosmetic(true);
                    m_previewLineItem = m_graphicsScene->addLine(QLineF(scenePos, scenePos), previewPen);
                } else {
                    if (m_previewLineItem) {
                        m_graphicsScene->removeItem(m_previewLineItem);
                        delete m_previewLineItem;
                        m_previewLineItem = nullptr;
                    }
                    const double distMm = measureDistanceMm(m_measureStartScene, scenePos);
                    m_hasMeasureStart = false;
                    statusBar()->showMessage(QStringLiteral("Medição registrada: %1 mm").arg(distMm, 0, 'f', 2));
                }
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton && m_isRoiMode) {
                const QPointF scenePos = m_graphicsView->mapToScene(mouseEvent->pos());
                if (!m_hasMeasureStart) {
                    m_measureStartScene = scenePos;
                    m_hasMeasureStart = true;
                    QPen previewPen(QColor(255, 213, 79), 1.5, Qt::DashLine);
                    previewPen.setCosmetic(true);
                    m_previewRectItem = m_graphicsScene->addRect(QRectF(scenePos, scenePos), previewPen);
                } else {
                    if (m_previewRectItem) {
                        m_graphicsScene->removeItem(m_previewRectItem);
                        delete m_previewRectItem;
                        m_previewRectItem = nullptr;
                    }
                    const RoiStatistics stats = measureRoiStats(QRectF(m_measureStartScene, scenePos));
                    m_hasMeasureStart = false;
                    if (stats.valid) {
                        statusBar()->showMessage(
                            QStringLiteral("ROI: Média=%1 | DP=%2 | Min=%3 | Max=%4 | Área=%5 mm²")
                                .arg(stats.mean, 0, 'f', 1)
                                .arg(stats.stdDev, 0, 'f', 1)
                                .arg(stats.minVal, 0, 'f', 0)
                                .arg(stats.maxVal, 0, 'f', 0)
                                .arg(stats.areaMm2, 0, 'f', 1));
                    }
                }
                return true;
            }
            if (mouseEvent->button() == Qt::RightButton && m_currentData.isMonochrome) {
                m_isAdjustingWindow = true;
                m_lastMousePos = mouseEvent->pos();
                m_graphicsView->viewport()->setCursor(Qt::SizeAllCursor);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_isMeasuringMode && m_hasMeasureStart && m_previewLineItem) {
                const QPointF scenePos = m_graphicsView->mapToScene(mouseEvent->pos());
                m_previewLineItem->setLine(QLineF(m_measureStartScene, scenePos));
                const double distMm = std::hypot(
                    (scenePos.x() - m_measureStartScene.x()) * m_currentData.pixelSpacingX,
                    (scenePos.y() - m_measureStartScene.y()) * m_currentData.pixelSpacingY);
                statusBar()->showMessage(QStringLiteral("Medindo: %1 mm").arg(distMm, 0, 'f', 1));
                return true;
            }
            if (m_isRoiMode && m_hasMeasureStart && m_previewRectItem) {
                const QPointF scenePos = m_graphicsView->mapToScene(mouseEvent->pos());
                m_previewRectItem->setRect(QRectF(m_measureStartScene, scenePos).normalized());
                return true;
            }
            if (m_isAdjustingWindow) {
                const QPoint delta = mouseEvent->pos() - m_lastMousePos;
                m_lastMousePos = mouseEvent->pos();

                const double dynamicStep = std::max(1.0, (m_currentData.maxPixelValue - m_currentData.minPixelValue) / 500.0);
                const double newWidth = std::max(1.0, m_currentData.windowWidth + delta.x() * dynamicStep);
                const double newCenter = m_currentData.windowCenter + delta.y() * dynamicStep;
                setWindowLevelWidth(newCenter, newWidth);
                return true;
            }
            const QPointF scenePos = m_graphicsView->mapToScene(mouseEvent->pos());
            const QPoint origPt = mapScenePointToOriginalPixel(scenePos);
            double pixelVal = 0.0;
            if (DicomLoader::samplePixelValue(m_currentData, origPt.x(), origPt.y(), pixelVal)) {
                statusBar()->showMessage(
                    QStringLiteral("Pos: (%1, %2) | Valor Calibrado: %3 | WL: %4 | WW: %5")
                        .arg(origPt.x())
                        .arg(origPt.y())
                        .arg(pixelVal, 0, 'f', 1)
                        .arg(m_currentData.windowCenter, 0, 'f', 1)
                        .arg(m_currentData.windowWidth, 0, 'f', 1));
            }
        } else if (event->type() == QEvent::MouseButtonRelease && m_isAdjustingWindow) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                m_isAdjustingWindow = false;
                m_graphicsView->viewport()->setCursor((m_isMeasuringMode || m_isRoiMode) ? Qt::CrossCursor : Qt::ArrowCursor);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

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

const QString& MainWindow::lastErrorMessage() const {
    return m_lastError;
}

QString MainWindow::statusSummaryText() const {
    return m_infoHeaderLabel ? m_infoHeaderLabel->text() : QString();
}

bool MainWindow::isErrorBannerVisible() const {
    return m_errorBannerLabel && m_errorBannerLabel->isVisible();
}

double MainWindow::currentWindowCenter() const {
    return m_currentData.windowCenter;
}

double MainWindow::currentWindowWidth() const {
    return m_currentData.windowWidth;
}

int MainWindow::metadataRowCount() const {
    return m_metadataTable ? m_metadataTable->rowCount() : 0;
}

bool MainWindow::isMetadataPanelVisible() const {
    return m_metadataDock && m_metadataDock->isVisible();
}

} // namespace dicom
