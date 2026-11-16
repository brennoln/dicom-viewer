#pragma once

#include "core/DicomImageData.h"
#include <QString>

namespace dicom {

/**
 * @brief Responsável pela leitura, descompressão, validação, extração de metadados
 * e renderização de arquivos DICOM (.dcm) utilizando a biblioteca DCMTK.
 */
class DicomLoader {
public:
    DicomLoader();
    ~DicomLoader() = default;

    /**
     * @brief Verifica se o dicionário de dados do DCMTK está carregado e operacional.
     */
    static bool isDictionaryLoaded();

    /**
     * @brief Registra os codecs de descompressão do DCMTK (JPEG, JPEG Lossless, JPEG-LS, RLE).
     * Seguro para chamadas múltiplas (idempotente).
     */
    static void ensureCodecsRegistered();

    /**
     * @brief Carrega e processa um arquivo DICOM a partir do caminho no disco.
     * @param filePath Caminho para o arquivo .dcm
     * @return DicomImageData contendo a imagem QImage renderizada, metadados ou mensagem de erro
     */
    DicomImageData loadFile(const QString& filePath) const;

    /**
     * @brief Re-renderiza uma imagem monocromática já carregada aplicando novos valores de
     * Window Level (Center) e Window Width sem reler o arquivo do disco.
     */
    DicomImageData applyWindowLevel(const DicomImageData& baseData,
                                    double windowCenter,
                                    double windowWidth) const;

    /**
     * @brief Restaura o janelamento padrão original do arquivo DICOM (VOI LUT ou Window padrão).
     */
    DicomImageData resetWindowLevel(const DicomImageData& baseData) const;

    /**
     * @brief Obtém o valor de intensidade calibrado (após Modality LUT / Rescale Slope & Intercept,
     * como Hounsfield Units em CT) na coordenada (x, y) da imagem original.
     */
    static bool samplePixelValue(const DicomImageData& data, int x, int y, double& outValue);
};

} // namespace dicom
