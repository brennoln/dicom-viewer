#pragma once

#include "core/DicomImageData.h"
#include <QString>

namespace dicom {

/**
 * @brief Responsável pela leitura, descompressão, validação e conversão
 * de arquivos DICOM (.dcm) utilizando a biblioteca DCMTK.
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
     * @return DicomImageData contendo a imagem QImage renderizada ou mensagem de erro detalhada
     */
    DicomImageData loadFile(const QString& filePath) const;
};

} // namespace dicom
