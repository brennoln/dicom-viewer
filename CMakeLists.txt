cmake_minimum_required(VERSION 3.16)

project(DicomViewer
    VERSION 1.0.0
    DESCRIPTION "Visualizador de Imagens DICOM com Qt e C++17"
    LANGUAGES CXX
)

# Requisito Técnico: C++17 ou superior
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Integração automática de MOC, UIC e RCC do Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Busca de pacotes obrigatórios: Qt6 (ou Qt5 fallback) e DCMTK
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(DCMTK REQUIRED)

# Biblioteca Core (Leitura, Processamento, Anonimização LGPD e Auditoria DICOM)
add_library(dicom_core STATIC
    src/core/AuditLogger.h
    src/core/AuditLogger.cpp
    src/core/DicomAnonymizer.h
    src/core/DicomAnonymizer.cpp
    src/core/DicomImageData.h
    src/core/DicomLoader.h
    src/core/DicomLoader.cpp
)

target_include_directories(dicom_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DCMTK_INCLUDE_DIRS}
)

target_link_libraries(dicom_core PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    DCMTK::dcmdata
    DCMTK::dcmimgle
    DCMTK::dcmimage
    DCMTK::dcmjpeg
    DCMTK::dcmjpls
    DCMTK::ofstd
    DCMTK::oflog
)

# Biblioteca de Interface Gráfica (separada do main.cpp para permitir testes de GUI)
add_library(dicom_ui STATIC
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_include_directories(dicom_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dicom_ui PUBLIC
    dicom_core
    Qt${QT_VERSION_MAJOR}::Widgets
)

# Executável Principal da Aplicação
add_executable(dicom_viewer
    src/main.cpp
)

target_link_libraries(dicom_viewer PRIVATE
    dicom_ui
)

# Configuração para ocultar console extra em builds Release no Windows
if(WIN32)
    set_target_properties(dicom_viewer PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

# Suíte de Testes Automatizados (CTest + QtTest)
enable_testing()

add_executable(tst_environment
    tests/tst_environment.cpp
)

target_link_libraries(tst_environment PRIVATE
    dicom_ui
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME EnvironmentAndToolchainTest COMMAND tst_environment)

add_executable(tst_dicom_loader
    tests/tst_dicom_loader.cpp
)

target_compile_definitions(tst_dicom_loader PRIVATE
    TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_link_libraries(tst_dicom_loader PRIVATE
    dicom_core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME DicomLoaderCoreTest COMMAND tst_dicom_loader)

add_executable(tst_gui
    tests/tst_gui.cpp
)

target_compile_definitions(tst_gui PRIVATE
    TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_link_libraries(tst_gui PRIVATE
    dicom_ui
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME DicomViewerGuiTest COMMAND tst_gui)

add_executable(tst_dicom_edge_cases
    tests/tst_dicom_edge_cases.cpp
)

target_link_libraries(tst_dicom_edge_cases PRIVATE
    dicom_core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME DicomEdgeCasesTest COMMAND tst_dicom_edge_cases)

add_executable(tst_security
    tests/tst_security.cpp
)

target_compile_definitions(tst_security PRIVATE
    TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_link_libraries(tst_security PRIVATE
    dicom_core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME DicomSecurityAndComplianceTest COMMAND tst_security)
