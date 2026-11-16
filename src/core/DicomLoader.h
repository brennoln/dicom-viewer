#pragma once

#include "core/DicomImageData.h"
#include <QRect>
#include <QString>

namespace dicom {

/**
 * @brief Responsável pela leitura, descompressão, validação, extração de metadados,
 * navegação multi-frame, análise quantitativa (ROI) e renderização de arquivos DICOM (.dcm).
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
     * Seguro para chamadas múltiplas e concorrentes (thread-safe via std::call_once).
     */
    static void ensureCodecsRegistered();

    /**
     * @brief Carrega e processa um arquivo DICOM a partir do caminho no disco.
     * @param filePath Caminho para o arquivo .dcm
     * @param frameIndex Índice do frame inicial a decodificar (0 para single-frame)
     * @return DicomImageData contendo a imagem QImage renderizada, metadados ou mensagem de erro
     */
    DicomImageData loadFile(const QString& filePath, int frameIndex = 0) const;

    /**
     * @brief Carrega outro frame de um exame multi-frame já aberto em memória,
     * sem reler o arquivo do disco.
     */
    DicomImageData loadFrame(const DicomImageData& baseData, int frameIndex) const;

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

    /**
     * @brief Calcula estatísticas quantitativas (média, desvio padrão, min, max e área em mm²)
     * para uma Região de Interesse (ROI) retangular sobre a imagem calibrada.
     */
    static RoiStatistics computeRoiStatistics(const DicomImageData& data, const QRect& rect);
};

} // namespace dicom
