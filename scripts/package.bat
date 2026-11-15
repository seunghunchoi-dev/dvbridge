@echo off
rem DVBridge release packages. Keep this file ASCII.
rem   DVBridge-<ver>-setup.exe   installer (Program Files, keeps data\ and plugins\ on update)
rem   DVBridge-<ver>-win64.zip   portable
rem   DVBridge-voicepack-<ver>-setup.exe   natural voice add-on (separate download, optional)
rem   No vocoder is shipped (users obtain it themselves; the installer can copy one they have)
rem Run scripts\build.bat first. Output: C:\dev\DMR\dist
setlocal
set VER=0.1
set QTDIR=C:\Qt\6.11.3\mingw_64
set PATH=C:\Qt\Tools\mingw1310_64\bin;%QTDIR%\bin;%PATH%
set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
set B=C:\dev\DMR\build\dmrbridge
set D=C:\dev\DMR\dist
set S=%D%\_stage
if exist "%S%" rmdir /s /q "%S%"
mkdir "%S%" || exit /b 1
copy /y "%B%\DVBridge.exe" "%S%\" >nul || exit /b 1
windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --compiler-runtime "%S%\DVBridge.exe" >nul || exit /b 1
python -I "%~dp0package.py" "%B%" "%S%" "%D%" %VER% || exit /b 1
rmdir /s /q "%S%"
if not exist "%ISCC%" (echo [ERROR] Inno Setup not found: %ISCC% & exit /b 1)
"%ISCC%" /Q /DAppVer=%VER% /DStage=%D%\DVBridge-%VER%-win64 /DOutDir=%D% "%~dp0..\packaging\DVBridge.iss" || exit /b 1
if exist "%D%\DVBridge-voicepack-%VER%\voice\dvbridge_tts.exe" (
  "%ISCC%" /Q /DAppVer=%VER% /DStage=%D%\DVBridge-voicepack-%VER% /DOutDir=%D% "%~dp0..\packaging\VoicePack.iss" || exit /b 1
)
echo PACKAGE OK
