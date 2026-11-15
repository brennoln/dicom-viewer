#include "core/DicomLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QTransform>

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
#include <dcmtk/dcmdata/dcrledrg.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <dcmtk/dcmimage/diregist.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>

namespace dicom {

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

    const QByteArray safePath = toDcmtkSafePath(fileInfo.absoluteFilePath());
    DcmFileFormat fileFormat;
    const OFCondition loadStatus = fileFormat.loadFile(OFFilename(safePath.constData()));
    if (loadStatus.bad()) {
        result.errorMessage = QStringLiteral("Falha ao ler arquivo DICOM (%1). Verifique se o arquivo e um .dcm valido.")
                                  .arg(QString::fromLatin1(loadStatus.text()));
        return result;
    }

    DcmDataset* dataset = fileFormat.getDataset();
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

    // Constrói e descomprime a imagem via DicomImage (DCMTK dcmimgle/dcmimage)
    DicomImage dicomImage(&fileFormat, xfer);
    if (dicomImage.getStatus() != EIS_Normal) {
        result.errorMessage = QStringLiteral("Erro ao processar a imagem DICOM: %1")
                                  .arg(QString::fromLatin1(DicomImage::getString(dicomImage.getStatus())));
        return result;
    }

    // Configuração correta da escala de cinza (VOI LUT / Window Center & Width / MinMax)
    if (dicomImage.isMonochrome()) {
        const unsigned long voiLutCount = dicomImage.getVoiLutCount();
        const unsigned long windowCount = dicomImage.getWindowCount();

        if (voiLutCount > 0) {
            // Procura preset NORMAL se existir (ex: mamografia GE), senão usa o primeiro VOI LUT
            unsigned long selectedLut = 0;
            for (unsigned long i = 0; i < voiLutCount; ++i) {
                OFString explanation;
                const char* desc = dicomImage.getVoiLutExplanation(i, explanation);
                if (desc && QString::fromLatin1(desc).contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive)) {
                    selectedLut = i;
                    break;
                }
            }
            dicomImage.setVoiLut(selectedLut);
        } else if (windowCount > 0) {
            unsigned long selectedWin = 0;
            for (unsigned long i = 0; i < windowCount; ++i) {
                OFString explanation;
                const char* desc = dicomImage.getVoiWindowExplanation(i, explanation);
                if (desc && QString::fromLatin1(desc).contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive)) {
                    selectedWin = i;
                    break;
                }
            }
            dicomImage.setWindow(selectedWin);
        } else {
            // Caso o DICOM não traga janelamento explícito, calcula janela Min-Max real dos pixels
            dicomImage.setMinMaxWindow();
        }
    }

    const int imgWidth = static_cast<int>(dicomImage.getWidth());
    const int imgHeight = static_cast<int>(dicomImage.getHeight());
    if (imgWidth <= 0 || imgHeight <= 0) {
        result.errorMessage = QStringLiteral("Dimensoes de imagem decodificada invalidas.");
        return result;
    }

    const void* rawOutput = dicomImage.getOutputData(8 /* 8 bits por amostra para exibição */);
    if (!rawOutput) {
        result.errorMessage = QStringLiteral("Falha ao renderizar os pixels da imagem DICOM.");
        return result;
    }

    QImage renderedImage;
    if (dicomImage.isMonochrome()) {
        renderedImage = QImage(imgWidth, imgHeight, QImage::Format_Grayscale8);
        const auto* srcPixels = static_cast<const uchar*>(rawOutput);
        // Copia linha por linha respeitando o bytesPerLine (alinhamento de 4 bytes do QImage)
        for (int y = 0; y < imgHeight; ++y) {
            std::memcpy(renderedImage.scanLine(y),
                        srcPixels + static_cast<size_t>(y) * static_cast<size_t>(imgWidth),
                        static_cast<size_t>(imgWidth));
        }
    } else {
        renderedImage = QImage(imgWidth, imgHeight, QImage::Format_RGB888);
        const auto* srcPixels = static_cast<const uchar*>(rawOutput);
        const size_t rowBytes = static_cast<size_t>(imgWidth) * 3U;
        for (int y = 0; y < imgHeight; ++y) {
            std::memcpy(renderedImage.scanLine(y),
                        srcPixels + static_cast<size_t>(y) * rowBytes,
                        rowBytes);
        }
    }

    // Aplica transformações de orientação de detector definidas no DICOM (FieldOfViewRotation / HorizontalFlip)
    const QString fovFlip = getDcmString(dataset, DCM_FieldOfViewHorizontalFlip).toUpper();
    Float64 fovRotation = 0.0;
    dataset->findAndGetFloat64(DCM_FieldOfViewRotation, fovRotation);
    const int rotationDegrees = (static_cast<int>(std::lround(fovRotation)) % 360 + 360) % 360;

    if (rotationDegrees != 0) {
        QTransform rotTransform;
        rotTransform.rotate(static_cast<qreal>(rotationDegrees));
        renderedImage = renderedImage.transformed(rotTransform, Qt::SmoothTransformation);
    }

    if (fovFlip == QStringLiteral("YES") || fovFlip == QStringLiteral("Y")) {
        QTransform flipTransform;
        flipTransform.scale(-1.0, 1.0);
        renderedImage = renderedImage.transformed(flipTransform);
    }

    // Corrige proporção espacial caso os pixels não sejam quadrados (PixelSpacing Y != X)
    if (result.pixelSpacingX > 0.0 && result.pixelSpacingY > 0.0) {
        const double ratio = result.pixelSpacingY / result.pixelSpacingX;
        if (std::abs(ratio - 1.0) > 1e-3) {
            const int scaledHeight = std::max(1, static_cast<int>(std::lround(renderedImage.height() * ratio)));
            renderedImage = renderedImage.scaled(renderedImage.width(), scaledHeight,
                                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    result.image = renderedImage;
    result.width = renderedImage.width();
    result.height = renderedImage.height();
    result.isValid = true;
    return result;
}

} // namespace dicom
