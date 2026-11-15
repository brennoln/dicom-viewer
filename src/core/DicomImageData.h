#pragma once

#include <QImage>
#include <QString>
#include <array>

namespace dicom {

/**
 * @brief Estrutura contendo a imagem DICOM processada e suas propriedades essenciais
 * para cumprir o Escopo Funcional Mínimo (dimensões, escala de cinza e orientação).
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
};

} // namespace dicom
