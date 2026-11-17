#include "core/DicomLoader.h"
#include "core/DicomFileUtils.h"

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcrledrg.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <dcmtk/dcmimage/diregist.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dipixel.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#ifdef HAS_DCMTK_JPLS
#include <dcmtk/dcmjpls/djdecode.h>
#endif

namespace dicom {

struct DicomRenderContext {
    std::shared_ptr<DcmFileFormat> fileFormat;
    std::shared_ptr<DicomImage> dicomImage;
    E_TransferSyntax xfer = EXS_Unknown;
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

    addMetadataIfPresent(entries, dataset, DCM_PatientName, QStringLiteral("Nome do Paciente"));
    addMetadataIfPresent(entries, dataset, DCM_PatientID, QStringLiteral("ID do Paciente"));
    addMetadataIfPresent(entries, dataset, DCM_PatientSex, QStringLiteral("Sexo"));
    addMetadataIfPresent(entries, dataset, DCM_PatientAge, QStringLiteral("Idade"));
    addMetadataIfPresent(entries, dataset, DCM_PatientBirthDate, QStringLiteral("Data de Nascimento"));
    addMetadataIfPresent(entries, dataset, DCM_StudyDate, QStringLiteral("Data do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_StudyTime, QStringLiteral("Hora do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_StudyDescription, QStringLiteral("Descrição do Estudo"));
    addMetadataIfPresent(entries, dataset, DCM_SeriesDescription, QStringLiteral("Descrição da Série"));

    addMetadataIfPresent(entries, dataset, DCM_Modality, QStringLiteral("Modalidade"));
    addMetadataIfPresent(entries, dataset, DCM_Manufacturer, QStringLiteral("Fabricante"));
    addMetadataIfPresent(entries, dataset, DCM_ManufacturerModelName, QStringLiteral("Modelo"));
    addMetadataIfPresent(entries, dataset, DCM_BodyPartExamined, QStringLiteral("Região Examinada"));
    addMetadataIfPresent(entries, dataset, DCM_ViewPosition, QStringLiteral("Posição / Incidência"));
    addMetadataIfPresent(entries, dataset, DCM_ImageLaterality, QStringLiteral("Lateralidade"));
    addMetadataIfPresent(entries, dataset, DCM_PatientOrientation, QStringLiteral("Orientação do Paciente"));
    addMetadataIfPresent(entries, dataset, DCM_SliceThickness, QStringLiteral("Espessura de Corte (mm)"));

    entries.append({QStringLiteral("(0028,0011/0010)"),
                    QStringLiteral("Dimensões (Col x Lin)"),
                    QStringLiteral("%1 x %2 px").arg(info.width).arg(info.height)});
    if (info.frameCount > 1) {
        entries.append({formatTagKey(DCM_NumberOfFrames),
                        QStringLiteral("Total de Frames"),
                        QString::number(info.frameCount)});
    }
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

    const void* rawOutput = dicomImage.getOutputData(8);
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
#if PACKAGE_VERSION_NUMBER >= 370
            OFString explanation;
            const char* desc = dicomImage.getVoiLutExplanation(i, explanation);
#else
            const char* desc = dicomImage.getVoiLutExplanation(i);
#endif
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
#if PACKAGE_VERSION_NUMBER >= 370
            OFString explanation;
            const char* desc = dicomImage.getVoiWindowExplanation(i, explanation);
#else
            const char* desc = dicomImage.getVoiWindowExplanation(i);
#endif
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

double readCalibratedPixelAt(const DiPixel& interData, size_t idx) {
    const void* raw = interData.getData();
    switch (interData.getRepresentation()) {
    case EPR_Uint8:  return static_cast<double>(static_cast<const Uint8*>(raw)[idx]);
    case EPR_Sint8:  return static_cast<double>(static_cast<const Sint8*>(raw)[idx]);
    case EPR_Uint16: return static_cast<double>(static_cast<const Uint16*>(raw)[idx]);
    case EPR_Sint16: return static_cast<double>(static_cast<const Sint16*>(raw)[idx]);
    case EPR_Uint32: return static_cast<double>(static_cast<const Uint32*>(raw)[idx]);
    case EPR_Sint32: return static_cast<double>(static_cast<const Sint32*>(raw)[idx]);
    default:         return 0.0;
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
#ifdef HAS_DCMTK_JPLS
        DJLSDecoderRegistration::registerCodecs();
#endif
        DcmRLEDecoderRegistration::registerCodecs();
    });
}

DicomImageData DicomLoader::loadFile(const QString& filePath, int frameIndex) const {
    ensureCodecsRegistered();

    DicomImageData result;
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Nenhum arquivo foi selecionado (caminho vazio).");
        return result;
    }

    auto fileFormat = std::make_shared<DcmFileFormat>();
    if (!file_utils::loadDicomFormatSafe(trimmedPath, *fileFormat, result.errorMessage)) {
        return result;
    }

    DcmDataset* dataset = fileFormat->getDataset();
    if (!dataset) {
        result.errorMessage = QStringLiteral("Arquivo DICOM invalido: dataset ausente.");
        return result;
    }

    Uint16 rows = 0;
    Uint16 cols = 0;
    const bool hasRows = dataset->findAndGetUint16(DCM_Rows, rows).good();
    const bool hasCols = dataset->findAndGetUint16(DCM_Columns, cols).good();
    if (!hasRows || !hasCols || rows == 0 || cols == 0) {
        result.errorMessage = QStringLiteral("Arquivo DICOM nao contem dimensoes de imagem validas (Rows/Columns).");
        return result;
    }

    constexpr Uint16 kMaxDimension = 16384;
    constexpr quint64 kMaxPixelCount = 100000000ULL;
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

    Uint16 bitsAllocated = 0;
    Uint16 bitsStored = 0;
    Uint16 pixelRep = 0;
    dataset->findAndGetUint16(DCM_BitsAllocated, bitsAllocated);
    dataset->findAndGetUint16(DCM_BitsStored, bitsStored);
    dataset->findAndGetUint16(DCM_PixelRepresentation, pixelRep);

    Sint32 numberOfFrames = 1;
    if (dataset->findAndGetSint32(DCM_NumberOfFrames, numberOfFrames).bad() || numberOfFrames < 1) {
        numberOfFrames = 1;
    }

    if (frameIndex < 0 || frameIndex >= numberOfFrames) {
        result.errorMessage = QStringLiteral("Indice de frame (%1) fora do intervalo valido (0..%2).")
                                  .arg(frameIndex)
                                  .arg(numberOfFrames - 1);
        return result;
    }

    result.frameCount = static_cast<int>(numberOfFrames);
    result.currentFrame = frameIndex;
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

    auto dicomImage = std::make_shared<DicomImage>(
        fileFormat.get(), xfer, 0UL, static_cast<unsigned long>(frameIndex), 1UL);
    if (dicomImage->getStatus() != EIS_Normal) {
        result.errorMessage = QStringLiteral("Erro ao processar a imagem DICOM: %1")
                                  .arg(QString::fromLatin1(DicomImage::getString(dicomImage->getStatus())));
        return result;
    }

    auto ctx = std::make_shared<DicomRenderContext>();
    ctx->fileFormat = fileFormat;
    ctx->dicomImage = dicomImage;
    ctx->xfer = xfer;
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

DicomImageData DicomLoader::loadFrame(const DicomImageData& baseData, int frameIndex) const {
    DicomImageData updated = baseData;
    if (!baseData.isValid || !baseData.renderContext || !baseData.renderContext->fileFormat) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Nenhum exame carregado para navegar entre frames.");
        return updated;
    }

    if (frameIndex < 0 || frameIndex >= baseData.frameCount) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Indice de frame (%1) fora do intervalo valido (0..%2).")
                                   .arg(frameIndex)
                                   .arg(baseData.frameCount - 1);
        return updated;
    }

    auto newCtx = std::make_shared<DicomRenderContext>(*baseData.renderContext);
    auto newDcmImg = std::make_shared<DicomImage>(
        newCtx->fileFormat.get(),
        newCtx->xfer,
        0UL,
        static_cast<unsigned long>(frameIndex),
        1UL);

    if (newDcmImg->getStatus() != EIS_Normal) {
        updated.isValid = false;
        updated.errorMessage = QStringLiteral("Erro ao decodificar frame %1: %2")
                                   .arg(frameIndex)
                                   .arg(QString::fromLatin1(DicomImage::getString(newDcmImg->getStatus())));
        return updated;
    }

    newCtx->dicomImage = newDcmImg;
    if (updated.isMonochrome) {
        if (updated.hasWindow && updated.windowWidth > 0.0) {
            if (newDcmImg->setWindow(updated.windowCenter, updated.windowWidth) == 0) {
                applyDefaultVoi(*newDcmImg, *newCtx);
            }
        } else {
            applyDefaultVoi(*newDcmImg, *newCtx);
            syncWindowMetrics(*newDcmImg, updated);
        }
    }

    QString renderError;
    const QImage rendered = renderDicomImageToQImage(*newDcmImg,
                                                     newCtx->rotationDegrees,
                                                     newCtx->horizontalFlip,
                                                     newCtx->pixelSpacingY,
                                                     newCtx->pixelSpacingX,
                                                     renderError);
    if (rendered.isNull()) {
        updated.isValid = false;
        updated.errorMessage = renderError;
        return updated;
    }

    updated.image = rendered;
    updated.width = rendered.width();
    updated.height = rendered.height();
    updated.currentFrame = frameIndex;
    updated.renderContext = newCtx;
    updated.isValid = true;
    return updated;
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
        return updated;
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
    if (!interData || interData->getCount() == 0 || !interData->getData()) {
        return false;
    }
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
    if (idx >= interData->getCount()) {
        return false;
    }

    outValue = readCalibratedPixelAt(*interData, idx);
    return true;
}

RoiStatistics DicomLoader::computeRoiStatistics(const DicomImageData& data, const QRect& rect) {
    RoiStatistics stats;
    if (!data.isValid || !data.renderContext || !data.renderContext->dicomImage) {
        return stats;
    }
    const DicomImage& dcmImg = *data.renderContext->dicomImage;
    const int w = static_cast<int>(dcmImg.getWidth());
    const int h = static_cast<int>(dcmImg.getHeight());
    const QRect clamped = rect.normalized().intersected(QRect(0, 0, w, h));
    if (clamped.isEmpty()) {
        return stats;
    }

    const DiPixel* interData = dcmImg.getInterData();
    if (!interData || interData->getCount() == 0 || !interData->getData()) {
        return stats;
    }

    double sum = 0.0;
    double sumSq = 0.0;
    double minV = std::numeric_limits<double>::max();
    double maxV = std::numeric_limits<double>::lowest();
    int count = 0;

    for (int y = clamped.top(); y <= clamped.bottom(); ++y) {
        const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(w);
        for (int x = clamped.left(); x <= clamped.right(); ++x) {
            const size_t idx = rowBase + static_cast<size_t>(x);
            if (idx >= interData->getCount()) {
                continue;
            }
            const double val = readCalibratedPixelAt(*interData, idx);
            sum += val;
            sumSq += val * val;
            minV = std::min(minV, val);
            maxV = std::max(maxV, val);
            ++count;
        }
    }

    if (count == 0) {
        return stats;
    }

    const double mean = sum / static_cast<double>(count);
    const double variance = std::max(0.0, (sumSq / static_cast<double>(count)) - (mean * mean));
    stats.valid = true;
    stats.pixelCount = count;
    stats.mean = mean;
    stats.stdDev = std::sqrt(variance);
    stats.minVal = minV;
    stats.maxVal = maxV;
    stats.areaMm2 = static_cast<double>(count) * data.pixelSpacingX * data.pixelSpacingY;
    return stats;
}

} // namespace dicom
