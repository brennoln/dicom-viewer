# Relatório Técnico de Qualidade, Segurança e Testes — Visualizador DICOM Clínico

**Data:** 2026-11-16  
**Stack:** C++17 (GCC 16.2.0 UCRT64) • Qt 6.12.0 (Widgets + Test) • DCMTK 3.7.0 • CMake / Ninja / CTest • Cppcheck  

---

## 1. Visão Geral do Cliente (Clínica Médica)

Com base na análise comparativa de plataformas radiológicas consolidadas (**RadiAnt DICOM Viewer**, **Horos**, **OHIF Viewer** e **Weasis**), o software foi evoluído para atender ao fluxo real de uma clínica médica com foco em precisão diagnóstica, produtividade do médico e conformidade regulatória:

| Funcionalidade Clínica | Implementação no Software | Benefício para a Clínica |
| :--- | :--- | :--- |
| **Window Level / Window Width (WL/WW)** | Sliders, SpinBoxes e arraste com botão direito do mouse em tempo real + botão de Reset (`VOI LUT`) | Ajuste rápido de janelas de contraste (partes moles, osso, pulmão, mamografia) sem recarregar o arquivo. |
| **Régua de Medição Física (mm)** | Ferramenta interativa de clique-a-clique calculada com `PixelSpacing` / `ImagerPixelSpacing` | Medição precisa de lesões, nódulos e estruturas anatômicas diretamente sobre o exame. |
| **Leitura de Intensidade / Hounsfield Units (HU)** | Amostragem calibrada via `DicomImage::getInterData()` exibida na barra de status sob o cursor | Inspeção quantitativa de densidade (ex.: ar `-1000 HU`, osso `+1000 HU` em CT). |
| **Manipulação Visual Diagnóstica** | Inverter Contraste (Negativo), Rotacionar 90°, Espelhar Horizontal, Zoom, Pan e Drag & Drop | Comparação bilateral (mamografia/tórax) e agilidade na leitura de exames. |
| **Painel Lateral de Metadados DICOM** | Tabela `QDockWidget` filtrável por tag, atributo ou valor em tempo real | Consulta rápida de paciente, modalidade, incidência (`MLO`), lateralidade (`L`/`R`) e parâmetros técnicos. |
| **Exportação Clínica & LGPD** | Exportação em PNG/JPEG para laudo/prontuário e exportação DICOM anonimizada (`DicomAnonymizer`) | Compartilhamento seguro de casos clínicos e pesquisas em conformidade com a LGPD. |

---

## 2. Arquitetura de Segurança e Conformidade (LGPD)

Foram implementadas defesas específicas contra vulnerabilidades históricas de visualizadores DICOM (como *Decompression Bombs*, *fuzzing* de cabeçalhos malformados e vazamento de dados sensíveis):

1. **Proteção contra DoS / Exaustão de Memória (`DicomLoader`)**:
   - Validação prévia de dimensões máximas (`16384 x 16384` px) e teto de `100 Megapixels` antes da alocação de buffers de imagem.
   - Limite de `512 MB` para carregamento de streams e validação estrita de `SamplesPerPixel` (`1`, `3` ou `4`).
   - Processamento isolado do frame inicial (`fcount = 1`) em exames multi-frame para evitar consumo excessivo de RAM.
2. **Anonimização Irreversível LGPD (`DicomAnonymizer`)**:
   - Remoção/substituição de tags de PHI (*Protected Health Information*): `PatientName`, `PatientID`, `PatientBirthDate`, `PatientAddress`, `PatientSex`, `PatientAge`, `ReferringPhysicianName`, `InstitutionName`, `OperatorsName`, `AccessionNumber`, `StudyID`, etc.
   - Regeneração automática de `StudyInstanceUID`, `SeriesInstanceUID` e `SOPInstanceUID` e marcação padrão DICOM (`PatientIdentityRemoved = YES`, `DeidentificationMethod = LGPD_BASIC_PROFILE`).
3. **Trilha de Auditoria (`AuditLogger`)**:
   - Registro local append-only com timestamp UTC ISO-8601 para eventos `OPEN_SUCCESS`, `OPEN_FAILED`, `EXPORT_IMAGE` e `ANONYMIZE_EXPORT`.
   - Sanitização contra *log injection* (`\n`, `\r`) e registro apenas do nome base do arquivo para evitar exposição de caminhos sensíveis.
4. **Resiliência de Caminhos e Memória**:
   - Fallback automático via `QFile` + `DcmInputBufferStream` para caminhos Unicode/acentuados no Windows.
   - Cópia de pixels linha a linha respeitando alinhamento de memória (`scanLine`) e checagem de `QImage::isNull()`.

---

## 3. Resultados da Análise Estática de Código (`Cppcheck`)

**Comando executado:**
```bash
cppcheck --enable=warning,performance,portability --library=qt --std=c++17 --error-exitcode=1 --suppress=missingIncludeSystem -I src src
```

- **Arquivos analisados:** `AuditLogger.cpp`, `DicomAnonymizer.cpp`, `DicomLoader.cpp`, `main.cpp`, `MainWindow.cpp` (e respectivos headers).
- **Resultado:** **0 erros**, **0 avisos de segurança**, **0 avisos de portabilidade**, **0 avisos de performance** (código de saída `0`).

---

## 4. Resultados das Suítes de Testes Automatizados (`CTest + QtTest`)

**Execução completa (`ctest --output-on-failure`):** **5/5 suítes aprovadas (100%) em 1.96s**.

| # | Suíte (`CTest`) | Tempo | Escopo Verificado | Status |
| :-: | :--- | :-: | :--- | :-: |
| 1 | `EnvironmentAndToolchainTest` | 0.36s | Padrão C++17 (`__cplusplus >= 201703L`), dicionário DCMTK carregado, instanciação da `MainWindow`. | **PASSED** |
| 2 | `DicomLoaderCoreTest` | 0.22s | Erros em arquivos vazios/inexistentes/corrompidos/sem `PixelData`, `MONOCHROME1` 16-bit, `MONOCHROME2` stride não múltiplo de 4, `FieldOfViewRotation`, `PixelSpacing` não-quadrado e validação pixel a pixel de `anonymized_mamo.dcm` (`2394x2850`). | **PASSED** |
| 3 | `DicomViewerGuiTest` | 0.91s | Estado inicial da GUI, diálogo modal de erro + banner visual, recuperação após erro, filtro de metadados, WL/WW + reset de `VOI LUT`, inversão de contraste, rotação 90°, espelhamento horizontal e medição física em `mm`. | **PASSED** |
| 4 | `DicomEdgeCasesTest` | 0.18s | CT 16-bit *Signed* com valores negativos + `RescaleSlope/Intercept`, fallback de `WindowWidth = 0` para `MinMax`, `RGB` *interleaved* e *planar*, `TransferSyntax` (Implicit LE, Explicit LE/BE), multi-frame, imagem `1x1`, arquivo truncado, caminho Unicode acentuado e `samplePixelValue`. | **PASSED** |
| 5 | `DicomSecurityAndComplianceTest` | 0.28s | Proteção contra *Decompression Bomb* (`50000x50000`), rejeição de `SamplesPerPixel = 99`, **25 testes de Fuzzing** (mutações aleatórias de stream DICOM + cabeçalho PE falso `"MZ"`), validação completa de remoção de PHI na anonimização LGPD e integridade/sanitização do `AuditLogger`. | **PASSED** |
