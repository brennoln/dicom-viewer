@echo off
setlocal
cd /d "%~dp0"
set PATH=C:\msys64\ucrt64\bin;%PATH%
start "" "%~dp0build\dicom_viewer.exe" %*
