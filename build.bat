@echo off
SETLOCAL

set WIN_VER=10.0.20348.0
set CONFIGURATION=debug

set QTDIR=C:\Qt\5.15.2\msvc2019_64
set "MSBUILD_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin"

set "PATH=%QTDIR%\bin;%PATH%"
set "PATH=%MSBUILD_DIR%;%PATH%"

REM make sure the correct environment variables are set for qmake
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 %WIN_VER%
REM run qmake
qmake CONFIG+=%CONFIGURATION%  WINDOWS_TARGET_PLATFORM_VERSION=%WIN_VER% -Wall -spec win32-msvc -tp vc NifSkope.pro
REM run msbuild
msbuild NifSkope.vcxproj /t:Build /p:Configuration=%configuration% /m:2