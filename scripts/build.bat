@echo off
rem DMRBridge build + tests (local only, no internet). Keep this file ASCII.
rem Build via junction C:\dev\DMR (non-ASCII path issue with Qt tools).
setlocal
set ROOT=C:\dev\DMR\dmrbridge
if not exist "%ROOT%\CMakeLists.txt" (echo [ERROR] junction missing: mklink /J C:\dev\DMR "%USERPROFILE%\DMR" & exit /b 1)
set QTDIR=C:\Qt\6.11.3\mingw_64
set PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;C:\Qt\Tools\mingw1310_64\bin;%QTDIR%\bin;%PATH%
set B=C:\dev\DMR\build\dmrbridge
rem Folder with a vocoder plugin for the session tests (optional - your own vocoder_*.dll; never shipped)
if "%PLUG%"=="" if exist "C:\dev\DMR\build\main\src\plugins" set PLUG=C:\dev\DMR\build\main\src\plugins
if "%PLUG%"=="" set PLUG=%B%\plugins

cmake -S "%ROOT%" -B "%B%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%" ^
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build "%B%" || exit /b 1
windeployqt --no-translations "%B%\DMRBridge.exe" >nul
rem tests\oracle (captured reference packets) is local only - without it the black-box part is skipped
set ORACLE=
if exist "%ROOT%\tests\oracle\dmrd_group.hex" set ORACLE=%ROOT%\tests\oracle
"%B%\dmr_core_test.exe" %ORACLE% || exit /b 1
"%B%\settings_test.exe" || exit /b 1
"%B%\notice_test.exe" || exit /b 1
"%B%\dashboard_test.exe" || exit /b 1
"%B%\id_db_test.exe" || exit /b 1
rem Own AMBE+2 plugin (optional, its source is not in the public repository)
if not exist "%B%\ambe2_test.exe" goto no_ambe2
"%B%\ambe2_test.exe" || exit /b 1
if not exist "%B%\plugins" mkdir "%B%\plugins"
copy /y "%B%\vocoder_ambe2.dll" "%B%\plugins\" >nul
:no_ambe2

rem homebrew_test: fake master on 127.0.0.1:62041 (password "local")
start "" /b python -I "%ROOT%\tests\tools\fake_master.py" 25 62041 local "%B%\fake_master.log"
ping -n 2 127.0.0.1 >nul
"%B%\homebrew_test.exe" local 62041 || exit /b 1

rem session_test: starts its own fake master (QProcess) and acts as the USRP client
"%B%\session_test.exe" "%ROOT%\tests\tools\fake_master.py" "%PLUG%" || exit /b 1
"%B%\multiuser_test.exe" "%ROOT%\tests\tools\fake_master.py" "%PLUG%" || exit /b 1
echo BUILD OK
