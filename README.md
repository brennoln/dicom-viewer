# Visualizador DICOM com Qt e C++17

Aplicação desenvolvida para o Teste Técnico de Desenvolvedor C++ (Visualização DICOM com Qt). O sistema permite ao usuário selecionar um arquivo DICOM (`.dcm`) no disco e exibi-lo corretamente em uma interface gráfica moderna e responsiva com Qt Widgets, respeitando dimensões, escala de cinza e orientação do exame.

---

## 🎯 Escopo Funcional Atendido

1. **Seleção de Arquivos (GUI)**:
   - Botão rápido na barra superior e menu `Arquivo > Abrir DICOM...` (`Ctrl+O`) utilizando `QFileDialog` com filtros para extensões `.dcm`, `.dicom` e todos os arquivos.
   - Suporte a Drag & Drop diretamente sobre a janela e abertura via linha de comando (`./dicom_viewer <caminho_arquivo>`).
2. **Carregamento e Descompressão**:
   - Integração com a biblioteca padrão da indústria **DCMTK 3.7.0**.
   - Registro automático e thread-safe de decodificadores para sintaxes compactadas (*JPEG Lossless*, *JPEG-LS*, *RLE*).
3. **Exibição e Renderização Fiel**:
   - Respeito integral às dimensões nativas e alinhamento de memória em bytes de cada linha (`scanLine`), prevenindo qualquer distorção visual (*stride skew*).
   - Suporte completo a imagens monocromáticas (`MONOCHROME1` e `MONOCHROME2`) de 8, 12 e 16 bits (*signed* e *unsigned*).
   - Calibração de contraste com seleção de tabelas `VOI LUT` (ex: preset *NORMAL* de mamografias), `Window Center/Width` ou mapeamento Min-Max como fallback.
   - Preservação da proporção física da imagem através de `PixelSpacing` e tratamento de transformações de detector (`FieldOfViewRotation` e `FieldOfViewHorizontalFlip`).
4. **Interface Gráfica e Tratamento de Erros**:
   - Área central baseada em `QGraphicsView` e `QGraphicsScene` com suporte a navegação por arraste (*pan*), zoom dinâmico via roda do mouse (*scroll*) e botões de atalho (*Ajustar*, *100%*, *Zoom +*, *Zoom -*).
   - Redimensionamento suave (*smooth transform*) mantendo proporção de aspecto.
   - Tratamento robusto para arquivos inexistentes, vazios (0 bytes), não-DICOM ou corrompidos, exibindo mensagens de erro claras tanto em diálogo modal (`QMessageBox::critical`) quanto em banner visual na janela.

---

## 🏥 Diferenciais Clínicos e Segurança (LGPD)

- **Window Level / Window Width (WL/WW) + Presets Clínicos**: Sliders, SpinBoxes, arraste com botão direito do mouse, presets de CT (*Pulmão*, *Osso*, *Abdômen*, *Cérebro*) e botão de Reset para restaurar a `VOI LUT` original.
- **Navegação Multi-Frame e Modo Cine**: Controle de quadros/cortes e reprodução Cine em exames seriados.
- **Carregamento Assíncrono (`QtConcurrent`)**: Decodificação em thread separada para não congelar a interface clínica.
- **Painel Lateral de Metadados DICOM**: Tabela filtrável em tempo real por tag, atributo ou valor.
- **Ferramentas Clínicas**: Régua de medição física (`mm`), ROI quantitativa (Média, Desvio Padrão, Mín/Máx e Área em `mm²`), marcadores de orientação anatômica, leitura calibrada sob o cursor, inversão de contraste, rotação 90° e espelhamento horizontal (com mapeamento preciso de coordenadas).
- **Suporte a Imagens RGB**: Renderização de imagens `RGB` (*interleaved* e *planar*) além de `MONOCHROME1`/`MONOCHROME2`.
- **Segurança & Conformidade LGPD**:
  - Proteção anti-DoS contra *Decompression Bomb* (limites de dimensões e memória antes da alocação).
  - Exportação DICOM anonimizada (`DicomAnonymizer`) removendo PHI e regenerando UIDs.
  - Trilha de auditoria local (`AuditLogger`) e exportação de imagem PNG/JPG para laudos.
- **Relatório Técnico Completo**: Consulte [docs/RELATORIO_QUALIDADE_E_SEGURANCA.md](docs/RELATORIO_QUALIDADE_E_SEGURANCA.md).

---

## 🛠️ Tecnologias e Bibliotecas Utilizadas

- **Linguagem**: C++17 (`-Wall -Wextra -Wpedantic`, zero warnings)
- **Framework de Interface**: Qt 6.12.0 (módulos `QtCore`, `QtGui`, `QtWidgets`, `QtConcurrent` e `QtTest`)
- **Leitura e Processamento DICOM**: DCMTK 3.7.0 (`dcmdata`, `dcmimgle`, `dcmimage`, `dcmjpeg`, `dcmjpls`, `ofstd`, `oflog`)
- **Sistema de Build, CI e Testes**: CMake (>= 3.16), Ninja, CTest, Cppcheck e GitHub Actions
- **Compilador**: GCC (MinGW-w64 / UCRT64) 16.2.0

---

## 📁 Estrutura do Projeto

```text
dicom-viewer/
├── .github/workflows/
│   └── ci.yml                  # Pipeline CI (build + 6 suítes CTest + Cppcheck)
├── CMakeLists.txt              # Configuração modular de compilação e suíte de testes
├── README.md                   # Documentação do projeto e instruções multiplataforma
├── run.bat                     # Atalho de inicialização direta no Windows
├── .gitignore                  # Regras de exclusão Git para C++/Qt/CMake
├── assets/
│   └── anonymized_mamo.dcm     # Arquivo DICOM anônimo oficial de teste
├── docs/
│   └── RELATORIO_QUALIDADE_E_SEGURANCA.md
├── src/
│   ├── main.cpp                # Ponto de entrada da aplicação e parsing de argumentos
│   ├── core/
│   │   ├── AuditLogger.h/.cpp      # Trilha de auditoria clínica local (sem PHI)
│   │   ├── DicomAnonymizer.h/.cpp  # Anonimização irreversível LGPD
│   │   ├── DicomFileUtils.h/.cpp   # I/O seguro e fallback Unicode/stream
│   │   ├── DicomImageData.h        # Modelo de dados da imagem, ROI e metadados
│   │   └── DicomLoader.h/.cpp      # Leitura, codecs, multi-frame, ROI e renderização
│   └── ui/
│       ├── DicomViewport.h/.cpp    # Viewport radiológico (zoom, pan, régua, ROI, WL/WW)
│       ├── MetadataDockWidget.h/.cpp # Painel lateral filtrável de metadados DICOM
│       └── MainWindow.h/.cpp       # Orquestração principal da janela clínica Qt
└── tests/
    ├── tst_environment.cpp         # Suíte 1: Padrão C++17, dicionário DCMTK e janela
    ├── tst_dicom_loader.cpp        # Suíte 2: Validação de erros, formatos e exame oficial
    ├── tst_gui.cpp                 # Suíte 3: GUI, diálogos, zoom, ferramentas e ROI
    ├── tst_dicom_edge_cases.cpp    # Suíte 4: CT signed 16-bit, RGB, transfer syntaxes, Unicode
    ├── tst_security.cpp            # Suíte 5: Anti-DoS, 25 testes de Fuzzing, LGPD e AuditLogger
    └── tst_cpp_recruiter.cpp       # Suíte 6: RAII, 8 threads concorrentes, multi-frame e QBENCHMARK
```

---

## 🚀 Instruções de Configuração, Compilação e Execução

### Pré-requisitos

#### Windows (MSYS2 UCRT64)
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-dcmtk
```
Certifique-se de que `C:\msys64\ucrt64\bin` esteja no seu `PATH`.

#### Linux (Ubuntu / Debian)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev libdcmtk-dev libtiff-dev libpng-dev libjpeg-dev zlib1g-dev libxml2-dev
```

#### macOS (Homebrew)
```bash
brew install cmake ninja qt@6 dcmtk
```

---

### Compilação

Na pasta raiz do projeto (`dicom-viewer`):

```bash
# 1. Configurar o projeto com CMake e Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. Compilar o executável e a suíte de testes
cmake --build build
```

---

### Execução dos Testes Automatizados

Para executar todas as 6 suítes de testes com relatório detalhado:

```bash
ctest --test-dir build --output-on-failure -V
```

*(Em ambientes Linux headless/CI sem servidor gráfico, defina `QT_QPA_PLATFORM=offscreen` antes de executar o `ctest`.)*

---

### Executando a Aplicação

```bash
# Windows (via executável compilado ou script run.bat)
./build/dicom_viewer.exe
./build/dicom_viewer.exe assets/anonymized_mamo.dcm

# Linux / macOS
./build/dicom_viewer
./build/dicom_viewer assets/anonymized_mamo.dcm
```
