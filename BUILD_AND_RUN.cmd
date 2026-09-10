@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem KFCSX2 - build, repair runtime DLLs, and launch.
rem Safe rule: this script never deletes deps, bin, DLLs, plugins, or user data.
rem Put this file in the KFCSX2 repository root.
rem ============================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "ROOT=%%~fI"

if exist "%ROOT%\kfcsx2\CMakeLists.txt" goto root_ready
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"
if exist "%ROOT%\kfcsx2\CMakeLists.txt" goto root_ready

echo ERROR: KFCSX2 repository root was not found.
echo Put BUILD_AND_RUN.cmd in the repository root and run it again.
exit /b 10

:root_ready
echo ============================================================
echo KFCSX2 BUILD AND RUN
echo Root: %ROOT%
echo ============================================================

set "INSTALLER=%ROOT%\BUILD-INSTALL-KFCSX2-DLSS5.cmd"
set "BUILD_OK=0"

if exist "%INSTALLER%" (
  echo.
  echo [1/4] Running KFCSX2 DLSS5 build/install pipeline...
  call "%INSTALLER%" "%ROOT%"
  if not errorlevel 1 (
    set "BUILD_OK=1"
  ) else (
    echo.
    echo WARNING: BUILD-INSTALL-KFCSX2-DLSS5.cmd failed.
    echo Falling back to a direct CMake build without deleting the dependency prefix.
  )
) else (
  echo.
  echo [1/4] BUILD-INSTALL-KFCSX2-DLSS5.cmd not found.
  echo Using direct CMake fallback.
)

if "%BUILD_OK%"=="1" goto locate_exe

rem --------------------- direct CMake fallback ---------------------
where cmake.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: cmake.exe was not found on PATH.
  exit /b 20
)

if not exist "%ROOT%\deps" (
  echo ERROR: %ROOT%\deps is missing.
  echo Restore dependencies with BUILD-INSTALL-KFCSX2-DLSS5.cmd first.
  exit /b 21
)

set "BUILD_DIR=%ROOT%\build-kfcsx2"
set "GENERATOR="
where ninja.exe >nul 2>nul
if not errorlevel 1 set "GENERATOR=-G Ninja"

if not defined GENERATOR (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  )
)

if defined GENERATOR (
  echo.
  echo [2/4] Configuring Release with Ninja...
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%ROOT%\deps" ^
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
) else (
  echo.
  echo [2/4] Configuring Release with Visual Studio 2022...
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%ROOT%\deps" ^
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
)
if errorlevel 1 exit /b 22

echo.
echo [3/4] Building KFCSX2...
if defined GENERATOR (
  cmake --build "%BUILD_DIR%" --parallel
) else (
  cmake --build "%BUILD_DIR%" --config Release --parallel
)
if errorlevel 1 exit /b 23

:locate_exe
set "EXE="

for %%F in (
  "%ROOT%\build-kfcsx2\bin\KFCSX2.exe"
  "%ROOT%\build-kfcsx2\bin\Release\KFCSX2.exe"
  "%ROOT%\build-kfcsx2\kfcsx2-qt\Release\KFCSX2.exe"
  "%ROOT%\build-dlss5\bin\KFCSX2.exe"
  "%ROOT%\build-dlss5\bin\Release\KFCSX2.exe"
  "%ROOT%\build-dlss5\kfcsx2-qt\Release\KFCSX2.exe"
  "%ROOT%\build\bin\KFCSX2.exe"
  "%ROOT%\build\bin\Release\KFCSX2.exe"
  "%ROOT%\bin\KFCSX2.exe"
) do (
  if not defined EXE if exist "%%~F" set "EXE=%%~fF"
)

if not defined EXE (
  for /f "delims=" %%F in ('where /r "%ROOT%" KFCSX2.exe 2^>nul') do (
    if not defined EXE set "EXE=%%~fF"
  )
)

if not defined EXE (
  echo ERROR: Build finished but KFCSX2.exe could not be found.
  exit /b 30
)

for %%F in ("%EXE%") do set "EXEDIR=%%~dpF"
echo.
echo Built executable: %EXE%

rem --------------------- runtime repair ---------------------
echo.
echo [4/4] Repairing runtime DLL/plugin set without deleting anything...

if exist "%ROOT%\deps\bin" (
  for %%F in ("%ROOT%\deps\bin\*.dll") do if exist "%%~F" copy /D /Y "%%~F" "%EXEDIR%" >nul
)

rem If the repo's bin directory contains DLSS/ReShade/runtime DLLs, restore them beside the built exe.
if exist "%ROOT%\bin" (
  for %%F in ("%ROOT%\bin\*.dll") do if exist "%%~F" copy /D /Y "%%~F" "%EXEDIR%" >nul
  for %%F in ("%ROOT%\bin\*.addon64") do if exist "%%~F" copy /D /Y "%%~F" "%EXEDIR%" >nul
  for %%F in ("%ROOT%\bin\*.ini") do if exist "%%~F" copy /D /Y "%%~F" "%EXEDIR%" >nul
)

rem Prefer Qt's own deploy tool when available.
if exist "%ROOT%\deps\bin\windeployqt.exe" (
  "%ROOT%\deps\bin\windeployqt.exe" --release --no-translations "%EXE%" >nul 2>nul
)

rem Fallback Qt plugin restoration. These copies are additive only.
for %%P in (platforms imageformats iconengines styles tls networkinformation) do (
  if exist "%ROOT%\deps\plugins\%%P" xcopy /D /E /I /Y "%ROOT%\deps\plugins\%%P" "%EXEDIR%%%P" >nul
)

rem Resources are required when running directly from a CMake build folder.
if exist "%ROOT%\bin\resources" (
  if not exist "%EXEDIR%resources" mkdir "%EXEDIR%resources" >nul 2>nul
  xcopy /D /E /I /Y "%ROOT%\bin\resources" "%EXEDIR%resources" >nul
)

rem Copy ReShade shader folders when present.
if exist "%ROOT%\bin\reshade-shaders" xcopy /D /E /I /Y "%ROOT%\bin\reshade-shaders" "%EXEDIR%reshade-shaders" >nul

rem Report common runtime DLLs. Missing entries are warnings because some builds link them statically.
for %%D in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll SDL3.dll) do (
  if not exist "%EXEDIR%%%D" echo WARNING: %%D is not beside KFCSX2.exe.
)

echo.
echo Launching KFCSX2...
start "KFCSX2" /D "%EXEDIR%" "%EXE%"
exit /b 0
