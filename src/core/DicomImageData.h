#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <array>
#include <memory>

namespace dicom {

/**
 * @brief Entrada individual de metadado DICOM para exibição na interface.
 */
struct DicomMetadataEntry {
    QString tag;
    QString name;
    QString value;
};

struct DicomRenderContext;

/**
 * @brief Estrutura contendo a imagem DICOM processada, propriedades espaciais,
 * parâmetros de janelamento (WL/WW) e metadados extraídos.
 */
struct DicomImageData {
    bool isValid = false;
    QString errorMessage;

    QImage image;
    int width = 0;
    int height = 0;
    int bitsAllocated = 0;
    int bitsStored = 0;
    bool isSigned = false;
    bool isMonochrome = true;
    QString photometricInterpretation;
    QString transferSyntaxUid;
    QString modality;

    // Espaçamento de pixels (Row Spacing = Y, Column Spacing = X) em mm
    double pixelSpacingY = 1.0;
    double pixelSpacingX = 1.0;

    // Orientação do paciente (Row / Column direction, ex: "A\FR")
    QString patientOrientation;
    QString imageLaterality;
    QString viewPosition;

    // Image Orientation (Patient) - 6 cossenos diretores (3 da linha, 3 da coluna)
    bool hasOrientation = false;
    std::array<double, 6> imageOrientation = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0};

    // Parâmetros de Window Level / Window Width (diferencial)
    bool hasWindow = false;
    double windowCenter = 0.0;
    double windowWidth = 0.0;
    double defaultWindowCenter = 0.0;
    double defaultWindowWidth = 0.0;
    double minPixelValue = 0.0;
    double maxPixelValue = 255.0;

    // Lista de metadados DICOM extraídos (diferencial)
    QVector<DicomMetadataEntry> metadata;

    // Contexto interno mantido em memória para re-renderização rápida de WL/WW
    std::shared_ptr<DicomRenderContext> renderContext;
};

} // namespace dicom
