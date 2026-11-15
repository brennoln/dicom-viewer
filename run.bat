@echo off
setlocal
cd /d "%~dp0"
echo ========================================================
echo   Iniciando Visualizador DICOM (Qt + DCMTK)...
echo ========================================================
start "" "%~dp0build\dicom_viewer.exe" %*
