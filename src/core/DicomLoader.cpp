#include "core/DicomLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcistrmb.h>
#include <dcmtk/dcmdata/dcrledrg.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <dcmtk/dcmimage/diregist.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dipixel.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>

namespace dicom {

struct DicomRenderContext {
    std::shared_ptr<DcmFileFormat> fileFormat;
    std::shared_ptr<DicomImage> dicomImage;
    int rotationDegrees = 0;
    bool horizontalFlip = false;
    double pixelSpacingY = 1.0;
    double pixelSpacingX = 1.0;
    bool hasDefaultVoiLut = false;
    unsigned long defaultVoiLutIndex = 0;
    bool hasDefaultWindow = false;
    unsigned long defaultWindowIndex = 0;
};

namespace {

std::once_flag g_codecsInitFlag;

/**
 * @brief Converte um caminho QString para uma representação nativa segura no Windows
 * (utilizando 8.3 Short Path quando disponível para evitar problemas de codepage ANSI
 * com caracteres acentuados no DCMTK).
 */
QByteArray toDcmtkSafePath(const QString& filePath) {
    const QString nativePath = QDir::toNativeSeparators(filePath);
#ifdef _WIN32
    const std::wstring widePath = nativePath.toStdWString();
    DWORD shortLen = ::GetShortPathNameW(widePath.c_str(), nullptr, 0);
    if (shortLen > 0) {
        std::wstring shortBuf(shortLen, L'\0');
        DWORD copied = ::GetShortPathNameW(widePath.c_str(), shortBuf.data(), shortLen);
        if (copied > 0 && copied < shortLen) {
            shortBuf.resize(copied);
            return QString::fromStdWString(shortBuf).toLocal8Bit();
        }
    }
#endif
    return nativePath.toLocal8Bit();
}

QString getDcmString(DcmDataset* dataset, const DcmTagKey& tag, const QString& defaultVal = QString()) {
    if (!dataset) {
        return defaultVal;
    }
    OFString val;
    if (dataset->findAndGetOFStringArray(tag, val).good() && !val.empty()) {
        return QString::fromLatin1(val.c_str()).trimmed();
    }
    return defaultVal;
}

QString formatTagKey(const DcmTagKey& tag) {
    return QStringLiteral("(%1,%2)")
        .arg(tag.getGroup(), 4, 16, QLatin1Char('0'))
        .arg(tag.getElement(), 4, 16, QLatin1Char('0'))
        .toUpper();
}

void addMetadataIfPresent(QVector<DicomMetadataEntry>& list,
                          DcmDataset* dataset,
                          const DcmTagKey& tag,
                          const QString& label) {
    const QString val = getDcmString(dataset, tag);
    if (!val.isEmpty()) {
        list.append({formatTagKey(tag), label, val});
    }
}

QVector<DicomMetadataEntry> extractMetadata(DcmDataset* dataset, const DicomImageData& info) {
    QVector<DicomMetadataEntry> entries;
    if (!dataset) {
        return entries;
    }

    // Informações do Paciente e Estudo
    addMetadataIfPresent(entries, dataset, DCM_PatientName, QStringLiteral("Nome do Paciente"));
    addMetadataIfPresent(entries, dataset, DCM_PatientID, QStringLiteral("ID do Paciente"));
    addMetadataIfPresent(entries, dataset, DCM_PatientSex, QStringLiteral("Sexo"));
    addMetadataIfPresent(entries, dataset, DCM_PatientAge, QStringLiteral("Idade"));
    addMetadataIfPresent(entries, dataset, DCM_PatientBirthDate, QStringLiteral("Data de Nascimento"));
    addMetadataIfPresent(entries, dataset, DCM_StudyDate, QStringLiteral("Data do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_StudyTime, QStringLiteral("Hora do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_StudyDescription, QStringLiteral("Descrição do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_SeriesDescription, QStringLiteral("Descrição da Série"));

    // Equipamento e Aquisição
    addMetadataIfPresent(entries, dataset, DCM_Modality, QStringLiteral("Modalidade"));
    addMetadataIfPresent(entries, dataset, DCM_Manufacturer, QStringLiteral("Fabricante"));
    addMetadataIfPresent(entries, dataset, DCM_ManufacturerModelName, QStringLiteral("Modelo"));
    addMetadataIfPresent(entries, dataset, DCM_BodyPartExamined, QStringLiteral("Região Examinada"));
    addMetadataIfPresent(entries, dataset, DCM_ViewPosition, QStringLiteral("Posição / Incidência"));
    addMetadataIfPresent(entries, dataset, DCM_ImageLaterality, QStringLiteral("Lateralidade"));
    addMetadataIfPresent(entries, dataset, DCM_PatientOrientation, QStringLiteral("Orientação do Paciente"));

    // Parâmetros Técnicos da Imagem
    entries.append({QStringLiteral("(0028,0011/0010)"),
                    QStringLiteral("Dimensões (Col x Lin)"),
                    QStringLiteral("%1 x %2 px").arg(info.width).arg(info.height)});
    entries.append({formatTagKey(DCM_PhotometricInterpretation),
                    QStringLiteral("Interpretação Fotométrica"),
                    info.photometricInterpretation});
    entries.append({QStringLiteral("(0028,0100/0101)"),
                    QStringLiteral("Profundidade de Bits"),
                    QStringLiteral("%1 bits armazenados (%2 alocados, %3)")
                        .arg(info.bitsStored)
                        .arg(info.bitsAllocated)
                        .arg(info.isSigned ? QStringLiteral("Signed") : QStringLiteral("Unsigned"))});
    entries.append({QStringLiteral("(0028,0030)"),
                    QStringLiteral("Pixel Spacing (Y x X)"),
                    QStringLiteral("%1 x %2 mm")
                        .arg(info.pixelSpacingY, 0, 'f', 3)
                        .arg(info.pixelSpacingX, 0, 'f', 3)});

    addMetadataIfPresent(entries, dataset, DCM_WindowCenter, QStringLiteral("Window Center (DICOM)"));
    addMetadataIfPresent(entries, dataset, DCM_WindowWidth, QStringLiteral("Window Width (DICOM)"));
    addMetadataIfPresent(entries, dataset, DCM_RescaleIntercept, QStringLiteral("Rescale Intercept"));
    addMetadataIfPresent(entries, dataset, DCM_RescaleSlope, QStringLiteral("Rescale Slope"));

    if (!info.transferSyntaxUid.isEmpty()) {
        entries.append({formatTagKey(DCM_TransferSyntaxUID),
                        QStringLiteral("Transfer Syntax"),
                        info.transferSyntaxUid});
    }

    return entries;
}

QImage renderDicomImageToQImage(DicomImage& dicomImage,
                                int rotationDegrees,
                                bool horizontalFlip,
                                double pixelSpacingY,
                                double pixelSpacingX,
                                QString& outError) {
    const int imgWidth = static_cast<int>(dicomImage.getWidth());
    const int imgHeight = static_cast<int>(dicomImage.getHeight());
    if (imgWidth <= 0 || imgHeight <= 0) {
        outError = QStringLiteral("Dimensoes de imagem decodificada invalidas.");
        return {};
    }

    const void* rawOutput = dicomImage.getOutputData(8 /* 8 bits por amostra para exibição */);
    if (!rawOutput) {
        outError = QStringLiteral("Falha ao renderizar os pixels da imagem DICOM.");
        return {};
    }

    QImage renderedImage;
    if (dicomImage.isMonochrome()) {
        renderedImage = QImage(imgWidth, imgHeight, QImage::Format_Grayscale8);
        if (renderedImage.isNull()) {
            outError = QStringLiteral("Memoria insuficiente para alocar a imagem monocromatica.");
            return {};
        }
        const auto* srcPixels = static_cast<const uchar*>(rawOutput);
        // Copia linha por linha respeitando o bytesPerLine (alinhamento de 4 bytes do QImage)
        for (int y = 0; y < imgHeight; ++y) {
            std::memcpy(renderedImage.scanLine(y),
                        srcPixels + static_cast<size_t>(y) * static_cast<size_t>(imgWidth),
                        static_cast<size_t>(imgWidth));
        }
    } else {
        renderedImage = QImage(imgWidth, imgHeight, QImage::Format_RGB888);
        if (renderedImage.isNull()) {
            outError = QStringLiteral("Memoria insuficiente para alocar a imagem RGB.");
            return {};
        }
        const auto* srcPixels = static_cast<const uchar*>(rawOutput);
        const size_t rowBytes = static_cast<size_t>(imgWidth) * 3U;
        for (int y = 0; y < imgHeight; ++y) {
            std::memcpy(renderedImage.scanLine(y),
                        srcPixels + static_cast<size_t>(y) * rowBytes,
                        rowBytes);
        }
    }

    if (rotationDegrees != 0) {
        QTransform rotTransform;
        rotTransform.rotate(static_cast<qreal>(rotationDegrees));
        renderedImage = renderedImage.transformed(rotTransform, Qt::SmoothTransformation);
    }

    if (horizontalFlip) {
        QTransform flipTransform;
        flipTransform.scale(-1.0, 1.0);
        renderedImage = renderedImage.transformed(flipTransform);
    }

    if (pixelSpacingX > 0.0 && pixelSpacingY > 0.0) {
        const double ratio = pixelSpacingY / pixelSpacingX;
        if (std::abs(ratio - 1.0) > 1e-3) {
            const int scaledHeight = std::max(1, static_cast<int>(std::lround(renderedImage.height() * ratio)));
            renderedImage = renderedImage.scaled(renderedImage.width(), scaledHeight,
                                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (renderedImage.isNull() || renderedImage.width() <= 0 || renderedImage.height() <= 0) {
        outError = QStringLiteral("Falha ao finalizar transformacoes da imagem DICOM.");
        return {};
    }

    return renderedImage;
}

void applyDefaultVoi(DicomImage& dicomImage, DicomRenderContext& ctx) {
    const unsigned long voiLutCount = dicomImage.getVoiLutCount();
    const unsigned long windowCount = dicomImage.getWindowCount();
    bool voiApplied = false;

    if (voiLutCount > 0) {
        unsigned long selectedLut = 0;
        for (unsigned long i = 0; i < voiLutCount; ++i) {
            OFString explanation;
            const char* desc = dicomImage.getVoiLutExplanation(i, explanation);
            if (desc && QString::fromLatin1(desc).contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive)) {
                selectedLut = i;
                break;
            }
        }
        if (dicomImage.setVoiLut(selectedLut) != 0) {
            voiApplied = true;
            ctx.hasDefaultVoiLut = true;
            ctx.defaultVoiLutIndex = selectedLut;
        }
    }

    if (!voiApplied && windowCount > 0) {
        unsigned long selectedWin = 0;
        for (unsigned long i = 0; i < windowCount; ++i) {
            OFString explanation;
            const char* desc = dicomImage.getVoiWindowExplanation(i, explanation);
            if (desc && QString::fromLatin1(desc).contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive)) {
                selectedWin = i;
                break;
            }
        }
        if (dicomImage.setWindow(selectedWin) != 0) {
            voiApplied = true;
            ctx.hasDefaultWindow = true;
            ctx.defaultWindowIndex = selectedWin;
        }
    }

    if (!voiApplied) {
        dicomImage.setMinMaxWindow();
    }
}

void syncWindowMetrics(DicomImage& dicomImage, DicomImageData& data) {
    double minVal = 0.0;
    double maxVal = 255.0;
    if (dicomImage.getMinMaxValues(minVal, maxVal)) {
        data.minPixelValue = minVal;
        data.maxPixelValue = maxVal;
    }

    double center = 0.0;
    double width = 0.0;
    if (dicomImage.getWindow(center, width) && width > 0.0) {
        data.hasWindow = true;
        data.windowCenter = center;
        data.windowWidth = width;
    } else if (data.isMonochrome) {
        data.hasWindow = true;
        data.windowCenter = (data.minPixelValue + data.maxPixelValue) * 0.5;
        data.windowWidth = std::max(1.0, data.maxPixelValue - data.minPixelValue);
    }
}

} // namespace

DicomLoader::DicomLoader() {
    ensureCodecsRegistered();
}

bool DicomLoader::isDictionaryLoaded() {
    return dcmDataDict.isDictionaryLoaded();
}

void DicomLoader::ensureCodecsRegistered() {
    std::call_once(g_codecsInitFlag, []() {
        DJDecoderRegistration::registerCodecs();
        DJLSDecoderRegistration::registerCodecs();
        DcmRLEDecoderRegistration::registerCodecs();
    });
}

DicomImageData DicomLoader::loadFile(const QString& filePath) const {
    ensureCodecsRegistered();

    DicomImageData result;
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Nenhum arquivo foi selecionado (caminho vazio).");
        return result;
    }

    const QFileInfo fileInfo(trimmedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.errorMessage = QStringLiteral("O arquivo selecionado nao existe ou nao e valido: %1").arg(trimmedPath);
        return result;
    }

    if (fileInfo.size() == 0) {
        result.errorMessage = QStringLiteral("O arquivo selecionado esta vazio (0 bytes).");
        return result;
    }

    constexpr qint64 kMaxDicomFileBytes = 512LL * 1024LL * 1024LL; // 512 MB
    if (fileInfo.size() > kMaxDicomFileBytes) {
        result.errorMessage = QStringLiteral("Arquivo excede o limite de seguranca suportado (512 MB).");
        return result;
    }

    const QByteArray safePath = toDcmtkSafePath(fileInfo.absoluteFilePath());
    auto fileFormat = std::make_shared<DcmFileFormat>();
    OFCondition loadStatus = fileFormat->loadFile(OFFilename(safePath.constData()));

    // Fallback via QFile + DcmInputBufferStream para caminhos Unicode quando 8.3 short paths não estão disponíveis
    if (loadStatus.bad()) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray rawBytes = file.readAll();
            if (!rawBytes.isEmpty()) {
                DcmInputBufferStream stream;
                stream.setBuffer(rawBytes.constData(), static_cast<offile_off_t>(rawBytes.size()));
                stream.setEos();
                fileFormat->clear();
                fileFormat->transferInit();
                loadStatus = fileFormat->read(stream);
                fileFormat->transferEnd();
            }
        }
    }

    if (loadStatus.bad()) {
        result.errorMessage = QStringLiteral("Falha ao ler arquivo DICOM (%1). Verifique se o arquivo e um .dcm valido.")
                                  .arg(QString::fromLatin1(loadStatus.text()));
        return result;
    }

    DcmDataset* dataset = fileFormat->getDataset();
    if (!dataset) {
        result.errorMessage = QStringLiteral("Arquivo DICOM invalido: dataset ausente.");
        return result;
    }

    // Verifica a existência de dados de pixel e dimensões válidas
    Uint16 rows = 0;
    Uint16 cols = 0;
    const bool hasRows = dataset->findAndGetUint16(DCM_Rows, rows).good();
    const bool hasCols = dataset->findAndGetUint16(DCM_Columns, cols).good();
    if (!hasRows || !hasCols || rows == 0 || cols == 0) {
        result.errorMessage = QStringLiteral("Arquivo DICOM nao contem dimensoes de imagem validas (Rows/Columns).");
        return result;
    }

    // Proteção de segurança contra Decompression Bomb / exaustão de memória (DoS)
    constexpr Uint16 kMaxDimension = 16384;
    constexpr quint64 kMaxPixelCount = 100000000ULL; // 100 Megapixels
    const quint64 totalPixels = static_cast<quint64>(rows) * static_cast<quint64>(cols);
    if (rows > kMaxDimension || cols > kMaxDimension || totalPixels > kMaxPixelCount) {
        result.errorMessage = QStringLiteral("Dimensoes de imagem (%1x%2) excedem o limite seguro de alocacao.")
                                  .arg(cols)
                                  .arg(rows);
        return result;
    }

    Uint16 samplesPerPixel = 1;
    dataset->findAndGetUint16(DCM_SamplesPerPixel, samplesPerPixel);
    if (samplesPerPixel != 1 && samplesPerPixel != 3 && samplesPerPixel != 4) {
        result.errorMessage = QStringLiteral("Numero de canais SamplesPerPixel (%1) nao suportado ou invalido.")
                                  .arg(samplesPerPixel);
        return result;
    }

    if (!dataset->tagExistsWithValue(DCM_PixelData) &&
        !dataset->tagExistsWithValue(DCM_FloatPixelData) &&
        !dataset->tagExistsWithValue(DCM_DoubleFloatPixelData)) {
        result.errorMessage = QStringLiteral("Arquivo DICOM nao possui dados de imagem (PixelData ausente).");
        return result;
    }

    // Extrai metadados de imagem e orientação
    Uint16 bitsAllocated = 0;
    Uint16 bitsStored = 0;
    Uint16 pixelRep = 0;
    dataset->findAndGetUint16(DCM_BitsAllocated, bitsAllocated);
    dataset->findAndGetUint16(DCM_BitsStored, bitsStored);
    dataset->findAndGetUint16(DCM_PixelRepresentation, pixelRep);

    result.bitsAllocated = static_cast<int>(bitsAllocated);
    result.bitsStored = static_cast<int>(bitsStored > 0 ? bitsStored : bitsAllocated);
    result.isSigned = (pixelRep == 1);
    result.photometricInterpretation = getDcmString(dataset, DCM_PhotometricInterpretation, QStringLiteral("MONOCHROME2"));
    result.modality = getDcmString(dataset, DCM_Modality);
    result.patientOrientation = getDcmString(dataset, DCM_PatientOrientation);
    result.imageLaterality = getDcmString(dataset, DCM_ImageLaterality);
    if (result.imageLaterality.isEmpty()) {
        result.imageLaterality = getDcmString(dataset, DCM_Laterality);
    }
    result.viewPosition = getDcmString(dataset, DCM_ViewPosition);

    const E_TransferSyntax xfer = dataset->getOriginalXfer();
    DcmXfer xferObj(xfer);
    result.transferSyntaxUid = QString::fromLatin1(xferObj.getXferID());

    // Extrai Pixel Spacing (Row Spacing \ Column Spacing) ou fallback para ImagerPixelSpacing
    Float64 spacingY = 0.0;
    Float64 spacingX = 0.0;
    if (dataset->findAndGetFloat64(DCM_PixelSpacing, spacingY, 0).good() &&
        dataset->findAndGetFloat64(DCM_PixelSpacing, spacingX, 1).good() &&
        spacingY > 0.0 && spacingX > 0.0) {
        result.pixelSpacingY = spacingY;
        result.pixelSpacingX = spacingX;
    } else if (dataset->findAndGetFloat64(DCM_ImagerPixelSpacing, spacingY, 0).good() &&
               dataset->findAndGetFloat64(DCM_ImagerPixelSpacing, spacingX, 1).good() &&
               spacingY > 0.0 && spacingX > 0.0) {
        result.pixelSpacingY = spacingY;
        result.pixelSpacingX = spacingX;
    }

    // Extrai Image Orientation (Patient) se presente (6 cossenos diretores)
    bool allCosinesFound = true;
    for (unsigned long i = 0; i < 6; ++i) {
        Float64 cosine = 0.0;
        if (dataset->findAndGetFloat64(DCM_ImageOrientationPatient, cosine, i).good()) {
            result.imageOrientation[i] = cosine;
        } else {
            allCosinesFound = false;
            break;
        }
    }
    result.hasOrientation = allCosinesFound;

    // Constrói e descomprime a imagem via DicomImage (apenas frame inicial para segurança em multi-frame)
    auto dicomImage = std::make_shared<DicomImage>(fileFormat.get(), xfer, 0UL /* flags */, 0UL /* fstart */, 1UL /* fcount */);
    if (dicomImage->getStatus() != EIS_Normal) {
        result.errorMessage = QStringLiteral("Erro ao processar a imagem DICOM: %1")
                                  .arg(QString::fromLatin1(DicomImage::getString(dicomImage->getStatus())));
        return result;
    }

    auto ctx = std::make_shared<DicomRenderContext>();
    ctx->fileFormat = fileFormat;
    ctx->dicomImage = dicomImage;
    ctx->pixelSpacingY = result.pixelSpacingY;
    ctx->pixelSpacingX = result.pixelSpacingX;

    const QString fovFlip = getDcmString(dataset, DCM_FieldOfViewHorizontalFlip).toUpper();
    ctx->horizontalFlip = (fovFlip == QStringLiteral("YES") || fovFlip == QStringLiteral("Y"));
    Float64 fovRotation = 0.0;
    dataset->findAndGetFloat64(DCM_FieldOfViewRotation, fovRotation);
    ctx->rotationDegrees = (static_cast<int>(std::lround(fovRotation)) % 360 + 360) % 360;

    result.isMonochrome = dicomImage->isMonochrome();
    if (result.isMonochrome) {
        applyDefaultVoi(*dicomImage, *ctx);
        syncWindowMetrics(*dicomImage, result);
        result.defaultWindowCenter = result.windowCenter;
        result.defaultWindowWidth = result.windowWidth;
    }

    QString renderError;
    const QImage renderedImage = renderDicomImageToQImage(*dicomImage,
                                                          ctx->rotationDegrees,
                                                          ctx->horizontalFlip,
                                                          ctx->pixelSpacingY,
                                                          ctx->pixelSpacingX,
                                                          renderError);
    if (renderedImage.isNull()) {
        result.errorMessage = renderError;
        return result;
    }

    result.image = renderedImage;
    result.width = renderedImage.width();
    result.height = renderedImage.height();
    result.renderContext = ctx;
    result.metadata = extractMetadata(dataset, result);
    result.isValid = true;
    return result;
}

DicomImageData DicomLoader::applyWindowLevel(const DicomImageData& baseData,
                                             double windowCenter,
                                             double windowWidth) const {
    DicomImageData updated = baseData;
    if (!baseData.isValid || !baseData.renderContext || !baseData.renderContext->dicomImage) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Nenhuma imagem carregada para aplicar Window Level/Width.");
        return updated;
    }

    if (!baseData.isMonochrome) {
        return updated; // WL/WW se aplica a imagens monocromáticas
    }

    const double safeWidth = std::max(1.0, windowWidth);
    auto& ctx = *baseData.renderContext;
    if (ctx.dicomImage->setWindow(windowCenter, safeWidth) == 0) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Falha ao aplicar Window Center/Width.");
        return updated;
    }

    QString renderError;
    const QImage renderedImage = renderDicomImageToQImage(*ctx.dicomImage,
                                                          ctx.rotationDegrees,
                                                          ctx.horizontalFlip,
                                                          ctx.pixelSpacingY,
                                                          ctx.pixelSpacingX,
                                                          renderError);
    if (renderedImage.isNull()) {
        updated.isValid = false;
        updated.errorMessage = renderError;
        return updated;
    }

    updated.image = renderedImage;
    updated.width = renderedImage.width();
    updated.height = renderedImage.height();
    updated.windowCenter = windowCenter;
    updated.windowWidth = safeWidth;
    updated.hasWindow = true;
    updated.isValid = true;
    return updated;
}

DicomImageData DicomLoader::resetWindowLevel(const DicomImageData& baseData) const {
    DicomImageData updated = baseData;
    if (!baseData.isValid || !baseData.renderContext || !baseData.renderContext->dicomImage) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Nenhuma imagem carregada para resetar Window Level/Width.");
        return updated;
    }

    if (!baseData.isMonochrome) {
        return updated;
    }

    auto& ctx = *baseData.renderContext;
    applyDefaultVoi(*ctx.dicomImage, ctx);
    if (ctx.hasDefaultVoiLut) {
        updated.windowCenter = baseData.defaultWindowCenter;
        updated.windowWidth = baseData.defaultWindowWidth;
        updated.hasWindow = baseData.hasWindow;
    } else {
        syncWindowMetrics(*ctx.dicomImage, updated);
    }

    QString renderError;
    const QImage renderedImage = renderDicomImageToQImage(*ctx.dicomImage,
                                                          ctx.rotationDegrees,
                                                          ctx.horizontalFlip,
                                                          ctx.pixelSpacingY,
                                                          ctx.pixelSpacingX,
                                                          renderError);
    if (renderedImage.isNull()) {
        updated.isValid = false;
        updated.errorMessage = renderError;
        return updated;
    }

    updated.image = renderedImage;
    updated.width = renderedImage.width();
    updated.height = renderedImage.height();
    updated.isValid = true;
    return updated;
}

bool DicomLoader::samplePixelValue(const DicomImageData& data, int x, int y, double& outValue) {
    if (!data.isValid || !data.renderContext || !data.renderContext->dicomImage) {
        return false;
    }
    const DicomImage& dcmImg = *data.renderContext->dicomImage;
    const int w = static_cast<int>(dcmImg.getWidth());
    const int h = static_cast<int>(dcmImg.getHeight());
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return false;
    }
    const DiPixel* interData = dcmImg.getInterData();
    if (!interData || interData->getCount() == 0) {
        return false;
    }
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
    if (idx >= interData->getCount()) {
        return false;
    }
    const void* raw = interData->getData();
    if (!raw) {
        return false;
    }

    switch (interData->getRepresentation()) {
    case EPR_Uint8:
        outValue = static_cast<double>(static_cast<const Uint8*>(raw)[idx]);
        return true;
    case EPR_Sint8:
        outValue = static_cast<double>(static_cast<const Sint8*>(raw)[idx]);
        return true;
    case EPR_Uint16:
        outValue = static_cast<double>(static_cast<const Uint16*>(raw)[idx]);
        return true;
    case EPR_Sint16:
        outValue = static_cast<double>(static_cast<const Sint16*>(raw)[idx]);
        return true;
    case EPR_Uint32:
        outValue = static_cast<double>(static_cast<const Uint32*>(raw)[idx]);
        return true;
    case EPR_Sint32:
        outValue = static_cast<double>(static_cast<const Sint32*>(raw)[idx]);
        return true;
    default:
        return false;
    }
}

} // namespace dicom
