# Relatório Técnico de Qualidade, Segurança e Testes — Visualizador DICOM Clínico

**Data:** 2026-11-16  
**Stack:** C++17 (GCC 16.2.0 UCRT64) • Qt 6.12.0 (Widgets + Concurrent + Test) • DCMTK 3.7.0 • CMake / Ninja / CTest • Cppcheck  

---

## 1. Visão Geral do Cliente (Clínica Médica)

Com base na análise comparativa de plataformas radiológicas consolidadas (**RadiAnt DICOM Viewer**, **Horos**, **OHIF Viewer** e **Weasis**), o software foi evoluído para atender ao fluxo real de uma clínica médica com foco em precisão diagnóstica, produtividade do médico e conformidade regulatória:

| Funcionalidade Clínica | Implementação no Software | Benefício para a Clínica |
| :--- | :--- | :--- |
| **Window Level / Window Width (WL/WW) + Presets Clínicos** | Sliders, SpinBoxes, arraste com botão direito e presets de CT (Pulmão, Osso, Abdômen, Cérebro) + Reset (`VOI LUT`) | Troca instantânea de janelas de contraste padrão em radiologia sem recarregar o arquivo. |
| **Navegação Multi-Frame / Modo Cine** | Barra dedicada com slider de corte/frame, botões Anterior/Próximo e playback Cine (10 fps) via `loadFrame` | Leitura fluida de exames seriados de Ultrassom, Tomografia e Ressonância multi-frame. |
| **Régua de Medição Física (mm) & ROI Quantitativa (HU)** | Medição clique-a-clique em `mm` e ROI retangular com cálculo de Média, Desvio Padrão, Mín/Máx e Área (`mm²`) | Análise quantitativa de lesões, cistos e estruturas anatômicas diretamente sobre o exame. |
| **Marcadores Anatômicos & Leitura sob o Cursor** | Overlays de orientação (`PatientOrientation` A/P/L/R/H/F) acompanhando rotação/espelhamento + valor calibrado na barra de status | Prevenção de desorientação espacial e inspeção instantânea de densidade tecidual. |
| **Carregamento Assíncrono (`QtConcurrent`)** | Decodificação fora da thread da interface gráfica via `QFutureWatcher<DicomImageData>` | Interface sempre responsiva mesmo ao abrir exames pesados de mamografia ou tomografia. |
| **Manipulação Visual Diagnóstica** | Inverter Contraste (Negativo), Rotacionar 90°, Espelhar Horizontal, Zoom, Pan e Drag & Drop | Comparação bilateral (mamografia/tórax) e agilidade na leitura de exames. |
| **Painel Lateral de Metadados DICOM** | Tabela `QDockWidget` filtrável por tag, atributo ou valor em tempo real | Consulta rápida de paciente, modalidade, incidência (`MLO`), lateralidade (`L`/`R`) e parâmetros técnicos. |
| **Exportação Clínica & LGPD** | Exportação em PNG/JPEG para laudo/prontuário e exportação DICOM anonimizada (`DicomAnonymizer`) | Compartilhamento seguro de casos clínicos e pesquisas em conformidade com a LGPD. |

---

## 2. Arquitetura de Segurança e Conformidade (LGPD)

Foram implementadas defesas específicas contra vulnerabilidades históricas de visualizadores DICOM (como *Decompression Bombs*, *fuzzing* de cabeçalhos malformados e vazamento de dados sensíveis):

1. **Proteção contra DoS / Exaustão de Memória (`DicomLoader`)**:
   - Validação prévia de dimensões máximas (`16384 x 16384` px) e teto de `100 Megapixels` antes da alocação de buffers de imagem.
   - Limite de `512 MB` para carregamento de streams e validação estrita de `SamplesPerPixel` (`1`, `3` ou `4`).
   - Processamento isolado por frame (`fcount = 1`) em exames multi-frame para evitar consumo excessivo de RAM.
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

## 3. Resultados da Análise Estática e Compilação Estrita

- **Flags do Compilador (`GCC 16.2.0`):** `-std=c++17 -Wall -Wextra -Wpedantic` habilitadas em `dicom_core` e `dicom_ui` com **0 warnings**.
- **Análise Estática (`Cppcheck`):**
  ```bash
  cppcheck --enable=warning,performance,portability --library=qt --std=c++17 --error-exitcode=1 --suppress=missingIncludeSystem -I src src
  ```
  - **Arquivos analisados:** `AuditLogger.cpp`, `DicomAnonymizer.cpp`, `DicomLoader.cpp`, `main.cpp`, `MainWindow.cpp` (e respectivos headers).
  - **Resultado:** **0 erros**, **0 avisos de segurança**, **0 avisos de portabilidade**, **0 avisos de performance** (código de saída `0`).

---

## 4. Resultados das Suítes de Testes Automatizados (`CTest + QtTest`)

**Execução completa (`ctest --output-on-failure`):** **6/6 suítes aprovadas (100%) em 2.32s**.

| # | Suíte (`CTest`) | Tempo | Escopo Verificado | Status |
| :-: | :--- | :-: | :--- | :-: |
| 1 | `EnvironmentAndToolchainTest` | 0.24s | Padrão C++17 (`__cplusplus >= 201703L`), dicionário DCMTK carregado, instanciação da `MainWindow`. | **PASSED** |
| 2 | `DicomLoaderCoreTest` | 0.22s | Erros em arquivos vazios/inexistentes/corrompidos/sem `PixelData`, `MONOCHROME1` 16-bit, `MONOCHROME2` stride não múltiplo de 4, `FieldOfViewRotation`, `PixelSpacing` não-quadrado e validação pixel a pixel de `anonymized_mamo.dcm` (`2394x2850`). | **PASSED** |
| 3 | `DicomViewerGuiTest` | 0.75s | Estado inicial da GUI, diálogo modal de erro + banner visual, recuperação após erro, filtro de metadados, WL/WW + reset de `VOI LUT`, inversão de contraste, rotação 90°, espelhamento horizontal e medição física em `mm`. | **PASSED** |
| 4 | `DicomEdgeCasesTest` | 0.22s | CT 16-bit *Signed* com valores negativos + `RescaleSlope/Intercept`, fallback de `WindowWidth = 0` para `MinMax`, `RGB` *interleaved* e *planar*, `TransferSyntax` (Implicit LE, Explicit LE/BE), multi-frame, imagem `1x1`, arquivo truncado, caminho Unicode acentuado e `samplePixelValue`. | **PASSED** |
| 5 | `DicomSecurityAndComplianceTest` | 0.28s | Proteção contra *Decompression Bomb* (`50000x50000`), rejeição de `SamplesPerPixel = 99`, **25 testes de Fuzzing** (mutações aleatórias de stream DICOM + cabeçalho PE falso `"MZ"`), validação completa de remoção de PHI (LGPD) e integridade/sanitização do `AuditLogger`. | **PASSED** |
| 6 | `CppRecruiterEvaluationTest` | 0.60s | RAII & *Move Semantics* (`shared_ptr` lifetime após destruição do `DicomLoader`), **concorrência em 8 threads paralelas** (`QtConcurrent`), navegação multi-frame (5 frames + bounds), estatísticas quantitativas de ROI (Média, DP, Área `mm²`), carregamento assíncrono na GUI + presets CT + benchmark `QBENCHMARK` (`4.1e-04 ms/iter` em 131.072 iterações). | **PASSED** |
