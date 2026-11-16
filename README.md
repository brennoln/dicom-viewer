# Visualizador DICOM com Qt e C++17

Aplicação desenvolvida para o Teste Técnico de Desenvolvedor C++ (Visualização DICOM com Qt). O sistema permite ao usuário selecionar um arquivo DICOM (`.dcm`) no disco e exibi-lo corretamente em uma interface gráfica moderna e responsiva com Qt Widgets, respeitando dimensões, escala de cinza e orientação do exame.

---

## 🎯 Escopo Funcional Atendido

1. **Seleção de Arquivos (GUI)**:
   - Botão rápido na barra superior e menu `Arquivo > Abrir Arquivo DICOM...` (`Ctrl+O`) utilizando `QFileDialog` com filtros para extensões `.dcm`, `.dicom` e todos os arquivos.
   - Suporte também para abertura via linha de comando (`./dicom_viewer <caminho_arquivo>`).
2. **Carregamento e Descompressão**:
   - Integração com a biblioteca padrão da indústria **DCMTK 3.7.0**.
   - Registro automático e transparente de decodificadores para sintaxes compactadas (*JPEG Lossless*, *JPEG-LS*, *RLE*).
3. **Exibição e Renderização Fiel**:
   - Respeito integral às dimensões nativas e alinhamento de memória em bytes de cada linha (`scanLine`), prevenindo qualquer distorção visual (*stride skew*).
   - Suporte completo a imagens monocromáticas (`MONOCHROME1` e `MONOCHROME2`) de 8, 12 e 16 bits.
   - Calibração de contraste com seleção de tabelas `VOI LUT` (ex: preset *NORMAL* de mamografias), `Window Center/Width` ou mapeamento Min-Max como fallback.
   - Preservação da proporção física da imagem através de `PixelSpacing` e tratamento de transformações de detector (`FieldOfViewRotation` e `FieldOfViewHorizontalFlip`).
4. **Interface Gráfica e Tratamento de Erros**:
   - Área central baseada em `QGraphicsView` e `QGraphicsScene` com suporte a navegação por arraste (*pan*), zoom dinâmico via roda do mouse (*scroll*) e botões de atalho (*Ajustar à Janela*, *Tamanho 100%*, *Zoom +*, *Zoom -*).
   - Redimensionamento suave (*smooth transform*) mantendo proporção de aspecto.
   - Tratamento robusto para arquivos inexistentes, vazios (0 bytes), não-DICOM ou corrompidos, exibindo mensagens de erro claras tanto em diálogo modal (`QMessageBox::critical`) quanto em banner visual na janela.

---

## 🏥 Diferenciais Clínicos e Segurança (LGPD)

- **Window Level / Window Width (WL/WW) em Tempo Real**: Sliders, SpinBoxes e arraste com botão direito do mouse, além de botão de Reset para restaurar a `VOI LUT` original do DICOM.
- **Painel Lateral de Metadados DICOM**: Tabela filtrável em tempo real por tag, atributo ou valor.
- **Ferramentas Clínicas**: Régua de medição física em milímetros (`mm`), leitura de intensidade calibrada / Hounsfield Units (`HU`) sob o cursor, inversão de contraste (negativo), rotação 90° e espelhamento horizontal.
- **Suporte a Imagens RGB**: Renderização de imagens `RGB` (*interleaved* e *planar*) além de `MONOCHROME1`/`MONOCHROME2`.
- **Segurança & Conformidade LGPD**:
  - Proteção anti-DoS contra *Decompression Bomb* (limites de dimensões e memória antes da alocação).
  - Exportação DICOM anonimizada (`DicomAnonymizer`) removendo PHI e regenerando UIDs.
  - Trilha de auditoria local (`AuditLogger`) e exportação de imagem PNG/JPG para laudos.
- **Relatório Técnico Completo**: Consulte [docs/RELATORIO_QUALIDADE_E_SEGURANCA.md](docs/RELATORIO_QUALIDADE_E_SEGURANCA.md).

---

## 🛠️ Tecnologias e Bibliotecas Utilizadas

- **Linguagem**: C++17
- **Framework de Interface**: Qt 6.12.0 (módulos `QtCore`, `QtGui`, `QtWidgets` e `QtTest`)
- **Leitura e Processamento DICOM**: DCMTK 3.7.0 (`dcmdata`, `dcmimgle`, `dcmimage`, `dcmjpeg`, `dcmjpls`, `ofstd`, `oflog`)
- **Sistema de Build e Testes**: CMake (>= 3.16), Ninja e CTest
- **Compilador**: GCC (MinGW-w64 / UCRT64) 16.2.0

---

## 📁 Estrutura do Projeto

```text
dicom-viewer/
├── CMakeLists.txt              # Configuração modular de compilação e suíte de testes
├── README.md                   # Documentação do projeto e instruções
├── .gitignore                  # Regras de exclusão Git para C++/Qt/CMake
├── assets/
│   └── anonymized_mamo.dcm     # Arquivo DICOM anônimo oficial de teste
├── src/
│   ├── main.cpp                # Ponto de entrada da aplicação e parsing de argumentos
│   ├── core/
│   │   ├── DicomImageData.h    # Modelo de dados da imagem e metadados
│   │   ├── DicomLoader.h       # Interface da classe leitora DICOM
│   │   └── DicomLoader.cpp     # Leitura, codecs, descompressão e conversão QImage
│   └── ui/
│       ├── MainWindow.h        # Declaração da janela principal Qt
│       └── MainWindow.cpp      # Implementação da GUI, canvas gráfico e interações
└── tests/
    ├── tst_environment.cpp     # Teste 1: Padrão C++17, dicionário DCMTK e janela
    ├── tst_dicom_loader.cpp    # Teste 2: Validação de erros, formatos e exame oficial
    └── tst_gui.cpp             # Teste 3: Interações de interface, diálogos e zoom
```

---

## 🚀 Instruções de Configuração, Compilação e Execução

### Pré-requisitos (Windows / MSYS2 UCRT64)

No terminal MSYS2 ou PowerShell configurado com o caminho dos binários:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-dcmtk
```

Certifique-se de que o diretório `C:\msys64\ucrt64\bin` esteja no seu `PATH`.

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

Para executar todos os testes de programação com relatório detalhado:

```bash
ctest --test-dir build --output-on-failure -V
```

Todas as 5 suítes de testes automatizados (`EnvironmentAndToolchainTest`, `DicomLoaderCoreTest`, `DicomViewerGuiTest`, `DicomEdgeCasesTest` e `DicomSecurityAndComplianceTest`) são executadas e verificam o funcionamento, casos de borda e segurança sem intervenção manual.

---

### Executando a Aplicação

Para abrir a aplicação normalmente:

```bash
# Via executável compilado
./build/dicom_viewer.exe

# Ou passando diretamente um arquivo DICOM para carregar na inicialização
./build/dicom_viewer.exe assets/anonymized_mamo.dcm
```
