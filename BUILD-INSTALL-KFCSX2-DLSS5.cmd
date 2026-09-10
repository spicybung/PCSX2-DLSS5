@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem PCSX2 + generic DLSS 5 build/install entry point.
rem Usage: BUILD-INSTALL-KFCSX2-DLSS5.cmd [PCSX2 source path]

set "ROOT=%~dp0"
set "PCSX2_SRC=%~1"
if not defined PCSX2_SRC set "PCSX2_SRC=C:\KFCSX2"

set "BUILD_DIR=!PCSX2_SRC!\build-dlss5"
set "RUNTIME_DIR=!PCSX2_SRC!\bin"
set "DEPS_DIR=!PCSX2_SRC!\deps"
set "DEPS_BUILD_DIR=!PCSX2_SRC!\deps-build"

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "SEVENZIP=C:\Program Files\7-Zip\7z.exe"

set "DEPS_ARCHIVE=%TEMP%\pcsx2-windows-dependencies.7z"
set "DEPS_URL=https://github.com/PCSX2/pcsx2-windows-dependencies/releases/download/latest-windows-dependencies/pcsx2-windows-dependencies.7z"

set "ONECLICK_DIR=!BUILD_DIR!\dlss5-tools"
set "ONECLICK=!ONECLICK_DIR!\dlss5oneclick.exe"
set "ONECLICK_URL=https://github.com/faisalkindi/DLSS5oneclick/releases/latest/download/dlss5oneclick.exe"
set "AIO_BASE=https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/latest/download"
set "AIO_ADDON_URL=!AIO_BASE!/standalone-dlssnr.addon64"
set "AIO_CALLER_URL=!AIO_BASE!/nvngx.dll"
set "AIO_SHADER_URL=!AIO_BASE!/DLSS5_AIO_Feed.fx"
set "VORT_ZIP_URL=https://codeload.github.com/vortigern11/vort_Shaders/zip/refs/heads/main"
set "VORT_ZIP=%TEMP%\vort_Shaders-main.zip"
set "VORT_TMP=%TEMP%\vort_Shaders-main-extract"

echo ============================================================
echo KFCSX2 DLSS 5 - build + runtime setup
echo Source: !PCSX2_SRC!
echo ============================================================

echo.
echo [0/6] Checking NVIDIA driver required by DLSS 5...
set "NV_DRIVER="

rem Do not use nvidia-smi CSV options here: older nvidia-smi builds differ.
rem Ask only for the driver_version field and parse the first numeric x.y token.
for /f "tokens=*" %%D in ('nvidia-smi --query-gpu=driver_version 2^>nul') do (
    if not defined NV_DRIVER (
        for /f "tokens=1" %%V in ("%%D") do (
            echo %%V| findstr /r /x "[0-9][0-9]*\.[0-9][0-9]*" >nul && set "NV_DRIVER=%%V"
        )
    )
)

if not defined NV_DRIVER (
    rem Fallback that works even when query output syntax is old/different.
    for /f "tokens=3" %%V in ('nvidia-smi 2^>nul ^| findstr /c:"Driver Version:"') do (
        if not defined NV_DRIVER set "NV_DRIVER=%%V"
    )
)

if not defined NV_DRIVER (
    echo ERROR: Could not parse the NVIDIA driver version from nvidia-smi.
    echo Run nvidia-smi manually and update the NVIDIA driver if it is older than 616.64.
    exit /b 1
)

for /f "tokens=1,2 delims=." %%A in ("!NV_DRIVER!") do (
    set "NV_MAJOR=%%A"
    set "NV_MINOR=%%B"
)

echo NVIDIA driver: !NV_DRIVER!
set /a NV_MAJOR_NUM=1!NV_MAJOR!-100 2>nul
set /a NV_MINOR_NUM=1!NV_MINOR!-100 2>nul

if !NV_MAJOR_NUM! LSS 616 goto driver_too_old
if !NV_MAJOR_NUM! EQU 616 if !NV_MINOR_NUM! LSS 64 goto driver_too_old
goto driver_ok

:driver_too_old
echo.
echo ERROR: NVIDIA driver !NV_DRIVER! predates the public DLSS 5 Game Ready driver.
echo DLSS 5 requires NVIDIA driver 616.64 or newer.
echo Update through NVIDIA App or NVIDIA's official driver page, reboot Windows,
echo then run this batch again.
start "" "https://www.nvidia.com/en-us/geforce/drivers/"
exit /b 2

:driver_ok
echo NVIDIA driver is new enough for the DLSS 5 runtime path.

if not exist "!VSROOT!\Common7\Tools\VsDevCmd.bat" (
    echo ERROR: VS2022 Build Tools not found at:
    echo        !VSROOT!
    exit /b 1
)

call "!VSROOT!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

for %%T in (git.exe python.exe cmake.exe) do (
    where %%T >nul 2>nul
    if errorlevel 1 (
        echo ERROR: %%T is not on PATH.
        exit /b 1
    )
)

if not exist "!SEVENZIP!" (
    echo ERROR: 7-Zip was not found at:
    echo        !SEVENZIP!
    exit /b 1
)

if exist "!PCSX2_SRC!\kfcsx2\GS\Renderers\DX11\GSDevice11.cpp" goto source_ready

if exist "!PCSX2_SRC!" (
    echo ERROR: !PCSX2_SRC! exists but is not a PCSX2 source tree.
    exit /b 1
)

echo PCSX2 source not found. Cloning current master...
git clone --recursive https://github.com/PCSX2/pcsx2.git "!PCSX2_SRC!"
if errorlevel 1 exit /b 1

:source_ready
git -C "!PCSX2_SRC!" submodule update --init --recursive
if errorlevel 1 exit /b 1

echo.
echo [1/5] Applying PCSX2 DLSS 5 source integration...
python "!ROOT!tools\dlss5\APPLY_TO_KFCSX2.py" "!PCSX2_SRC!"
if errorlevel 1 exit /b 1

echo.
echo [2/5] Checking PCSX2 dependencies...
python "!ROOT!tools\dlss5\CHECK_KFCSX2_DEPS.py" "!DEPS_DIR!"
if not errorlevel 1 goto deps_ready

echo.
echo The local dependency prefix is incomplete.
echo Using PCSX2's OFFICIAL PREBUILT Windows dependency package instead of
echo recompiling Qt and every third-party library from source.
echo.

rem A failed source dependency build can consume many GB.  Remove only its
rem temporary build tree first; the installed deps stay until the official
rem archive has downloaded successfully.
if exist "!DEPS_BUILD_DIR!" (
    echo Removing failed dependency build scratch tree to reclaim disk space...
    rmdir /s /q "!DEPS_BUILD_DIR!"
)

echo Downloading official PCSX2 Windows dependencies...
where curl.exe >nul 2>nul
if errorlevel 1 goto deps_download_ps

curl.exe -fL --retry 3 --retry-delay 2 -o "!DEPS_ARCHIVE!" "!DEPS_URL!"
if errorlevel 1 goto deps_download_fail
goto deps_downloaded

:deps_download_ps
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '!DEPS_URL!' -OutFile '!DEPS_ARCHIVE!'"
if errorlevel 1 goto deps_download_fail
goto deps_downloaded

:deps_download_fail
echo ERROR: Failed to download the official PCSX2 Windows dependency archive.
exit /b 1

:deps_downloaded
echo Replacing the partial dependency prefix with the official package...
if exist "!DEPS_DIR!" rmdir /s /q "!DEPS_DIR!"

"!SEVENZIP!" x "!DEPS_ARCHIVE!" -o"!PCSX2_SRC!" -y
if errorlevel 1 (
    echo ERROR: Failed to extract PCSX2 Windows dependencies.
    exit /b 1
)

python "!ROOT!tools\dlss5\CHECK_KFCSX2_DEPS.py" "!DEPS_DIR!"
if errorlevel 1 (
    echo ERROR: The official dependency package extracted, but required PCSX2 dependencies are still missing.
    exit /b 1
)

if exist "!DEPS_ARCHIVE!" del /q "!DEPS_ARCHIVE!" >nul 2>nul

:deps_ready
if exist "!DEPS_ARCHIVE!" del /q "!DEPS_ARCHIVE!" >nul 2>nul
echo PCSX2 dependency prefix is complete.

rem v7: the normal VS Release target has a deterministic location.
rem Reuse it if a previous v6 run already finished the compile; this makes the
rem script resume directly at runtime setup instead of rebuilding PCSX2.
set "PCSX2_EXE=!BUILD_DIR!\kfcsx2-qt\Release\KFCSX2.exe"

if exist "!PCSX2_EXE!" (
    echo.
    echo [3/5] Existing PCSX2 build found.
    echo [4/5] Rebuilding once for the v10 external-runtime guard...
    cmake --build "!BUILD_DIR!" --config Release --parallel
    if errorlevel 1 exit /b 1
    goto build_ready
)

echo.
echo [3/5] Configuring PCSX2...
if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"

cmake -S "!PCSX2_SRC!" -B "!BUILD_DIR!" ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_PREFIX_PATH="!DEPS_DIR!"
if errorlevel 1 (
    echo.
    echo ERROR: PCSX2 configuration failed after using the complete dependency package.
    exit /b 1
)

echo.
echo [4/5] Building PCSX2 Release...
cmake --build "!BUILD_DIR!" --config Release --parallel
if errorlevel 1 exit /b 1

rem Prefer the known CMake/VS output path.  If upstream changes it later,
rem fall back to WHERE /R, which returns only files that actually exist.
set "PCSX2_EXE=!BUILD_DIR!\kfcsx2-qt\Release\KFCSX2.exe"
if exist "!PCSX2_EXE!" goto build_ready

set "PCSX2_EXE="
for /f "delims=" %%F in ('where /r "!BUILD_DIR!" KFCSX2.exe 2^>nul') do (
    if not defined PCSX2_EXE set "PCSX2_EXE=%%F"
)

if not defined PCSX2_EXE (
    echo ERROR: Build completed but KFCSX2.exe was not found under !BUILD_DIR!.
    exit /b 1
)

:build_ready
if not exist "!PCSX2_EXE!" (
    echo ERROR: Resolved PCSX2 executable does not exist:
    echo        !PCSX2_EXE!
    exit /b 1
)

echo Built: !PCSX2_EXE!

echo.
echo [4.5/5] Staging a RUNNABLE PCSX2 folder...
rem PCSX2's Windows install rules deploy the runnable tree to the source
rem repository's bin directory.  The v8 attempt to force --prefix to a
rem dist-dlss5 directory was ignored by PCSX2's absolute install destinations,
rem which is why the files appeared under C:\KFCSX2\bin instead.
cmake --install "!BUILD_DIR!" --config Release
if errorlevel 1 (
    echo ERROR: PCSX2 install/staging step failed.
    exit /b 1
)

set "PCSX2_EXE=!RUNTIME_DIR!\KFCSX2.exe"

if not exist "!PCSX2_EXE!" (
    rem Future-proof fallback if PCSX2 changes its install layout later.
    set "PCSX2_EXE="
    for /f "delims=" %%F in ('where /r "!PCSX2_SRC!" KFCSX2.exe 2^>nul') do (
        if /i not "%%~dpF"=="!BUILD_DIR!\kfcsx2-qt\Release\" (
            if not defined PCSX2_EXE set "PCSX2_EXE=%%F"
        )
    )
)

if not defined PCSX2_EXE (
    echo ERROR: PCSX2 install completed but no runnable KFCSX2.exe was found.
    exit /b 1
)

if not exist "!PCSX2_EXE!" (
    echo ERROR: Resolved runtime executable does not exist:
    echo        !PCSX2_EXE!
    exit /b 1
)

for %%F in ("!PCSX2_EXE!") do set "PCSX2_OUT=%%~dpF"

rem The install step should already deploy all runtime DLLs.  Keep a defensive
rem fallback for dependency DLLs in case a future PCSX2 install rule omits one.
if exist "!DEPS_DIR!\bin\*.dll" copy /y "!DEPS_DIR!\bin\*.dll" "!PCSX2_OUT!" >nul

if not exist "!PCSX2_OUT!ryml.dll" (
    echo ERROR: ryml.dll was not staged beside KFCSX2.exe.
    exit /b 1
)
if not exist "!PCSX2_OUT!z.dll" (
    echo ERROR: z.dll was not staged beside KFCSX2.exe.
    exit /b 1
)

echo Runnable KFCSX2: !PCSX2_EXE!
echo Runtime folder: !PCSX2_OUT!

echo.
echo [5/5] Preparing generic DX11 DLSS 5 runtime setup...
if not exist "!ONECLICK_DIR!" mkdir "!ONECLICK_DIR!"

where curl.exe >nul 2>nul
if errorlevel 1 goto oneclick_download_ps

curl.exe -fL --retry 3 --retry-delay 2 -o "!ONECLICK!" "!ONECLICK_URL!"
if errorlevel 1 goto oneclick_download_fail
goto oneclick_downloaded

:oneclick_download_ps
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '!ONECLICK_URL!' -OutFile '!ONECLICK!'"
if errorlevel 1 goto oneclick_download_fail
goto oneclick_downloaded

:oneclick_download_fail
echo ERROR: Failed to download DLSS5oneclick.
exit /b 1

:oneclick_downloaded
rem PCSX2 imports both D3D11 and D3D12.  DLSS5oneclick detects the API from
rem the selected executable, so use a tiny DX11-only probe in the *same final
rem folder*.  DLSS5oneclick then installs its DX11/no-native-DLSS files into
rem that PCSX2 output directory rather than guessing DX12 from KFCSX2.exe.
echo Runtime target folder: !PCSX2_OUT!
set "PROBE_CPP=!PCSX2_OUT!_dlss5_dx11_probe.cpp"
set "PROBE_OBJ=!PCSX2_OUT!_dlss5_dx11_probe.obj"
set "PROBE_EXE=!PCSX2_OUT!_dlss5_dx11_probe.exe"

>"!PROBE_CPP!" echo #include ^<d3d11.h^>
>>"!PROBE_CPP!" echo int main(void) { ID3D11Device* d=0; ID3D11DeviceContext* c=0; D3D_FEATURE_LEVEL f; HRESULT h=D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,0,0,0,D3D11_SDK_VERSION,^&d,^&f,^&c); if(c)c-^>Release(); if(d)d-^>Release(); return FAILED(h); }

cl /nologo /EHsc "!PROBE_CPP!" /Fo:"!PROBE_OBJ!" /Fe:"!PROBE_EXE!" d3d11.lib >nul
if errorlevel 1 goto probe_fail

echo.
echo DLSS5oneclick detection:
"!ONECLICK!" "!PROBE_EXE!" --check
if errorlevel 1 goto oneclick_check_fail

echo.
echo Opening DLSS5oneclick already targeted at the final PCSX2 folder.
echo Click "Install DLSS 5" once in its window.  When it exits, this batch
echo cleans the temporary probe automatically.
"!ONECLICK!" "!PROBE_EXE!"
set "DLSS_RESULT=!ERRORLEVEL!"

del /q "!PROBE_CPP!" "!PROBE_OBJ!" "!PROBE_EXE!" >nul 2>nul

if not "!DLSS_RESULT!"=="0" (
    echo ERROR: DLSS5oneclick returned !DLSS_RESULT!.
    exit /b !DLSS_RESULT!
)

echo.
echo [6/6] Replacing the failing Feeder transport with Standalone DLSS-NR AIO...

rem Keep ReShade and the NVIDIA runtime DLLs installed by DLSS5oneclick.
rem Remove only the old synthetic-DLSS feeder/consumer pair which failed on
rem PCSX2 with E_INVALIDARG while closing its private D3D12 command list.
del /q "!PCSX2_OUT!dlss5-feed.addon64" >nul 2>nul
del /q "!PCSX2_OUT!renodx-dlss5.addon64" >nul 2>nul
del /q "!PCSX2_OUT!dlss5-feed.cfg" >nul 2>nul
del /q "!PCSX2_OUT!dlss5-feed.log" >nul 2>nul
del /q "!PCSX2_OUT!reshade-shaders\Shaders\DLSS5_Feed.fx" >nul 2>nul

if not exist "!PCSX2_OUT!reshade-shaders\Shaders" mkdir "!PCSX2_OUT!reshade-shaders\Shaders"

echo Downloading standalone DLSS-NR addon...
curl.exe -fL --retry 3 --retry-delay 2 -o "!PCSX2_OUT!standalone-dlssnr.addon64" "!AIO_ADDON_URL!"
if errorlevel 1 exit /b 1

echo Downloading caller bridge...
curl.exe -fL --retry 3 --retry-delay 2 -o "!PCSX2_OUT!nvngx.dll" "!AIO_CALLER_URL!"
if errorlevel 1 exit /b 1

echo Downloading AIO guide shader...
curl.exe -fL --retry 3 --retry-delay 2 -o "!PCSX2_OUT!reshade-shaders\Shaders\DLSS5_AIO_Feed.fx" "!AIO_SHADER_URL!"
if errorlevel 1 exit /b 1

echo Installing VORT Motion optical-flow provider required for real NR motion guides...
if exist "!VORT_ZIP!" del /q "!VORT_ZIP!" >nul 2>nul
if exist "!VORT_TMP!" rmdir /s /q "!VORT_TMP!"

where curl.exe >nul 2>nul
if errorlevel 1 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '!VORT_ZIP_URL!' -OutFile '!VORT_ZIP!'"
) else (
    curl.exe -fL --retry 3 --retry-delay 2 -o "!VORT_ZIP!" "!VORT_ZIP_URL!"
)
if errorlevel 1 (
    echo ERROR: Failed to download VORT Motion.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath '!VORT_ZIP!' -DestinationPath '!VORT_TMP!' -Force"
if errorlevel 1 (
    echo ERROR: Failed to extract VORT Motion.
    exit /b 1
)

if not exist "!VORT_TMP!\vort_Shaders-main\Shaders\vort_Motion.fx" (
    echo ERROR: VORT archive extracted but vort_Motion.fx was not found.
    exit /b 1
)

xcopy /e /i /y "!VORT_TMP!\vort_Shaders-main\Shaders\*" "!PCSX2_OUT!reshade-shaders\Shaders\" >nul
if errorlevel 1 (
    echo ERROR: Failed to install VORT shader files.
    exit /b 1
)

if exist "!VORT_TMP!\vort_Shaders-main\Textures" (
    if not exist "!PCSX2_OUT!reshade-shaders\Textures" mkdir "!PCSX2_OUT!reshade-shaders\Textures"
    xcopy /e /i /y "!VORT_TMP!\vort_Shaders-main\Textures\*" "!PCSX2_OUT!reshade-shaders\Textures\" >nul
    if errorlevel 1 (
        echo ERROR: Failed to install VORT texture files.
        exit /b 1
    )
)

if exist "!VORT_ZIP!" del /q "!VORT_ZIP!" >nul 2>nul
if exist "!VORT_TMP!" rmdir /s /q "!VORT_TMP!"

if not exist "!PCSX2_OUT!reshade-shaders\Shaders\vort_Motion.fx" (
    echo ERROR: VORT installation verification failed.
    exit /b 1
)
echo VORT Motion installed: !PCSX2_OUT!reshade-shaders\Shaders\vort_Motion.fx

if not exist "!PCSX2_OUT!nvngx_dlssnr.dll" (
    echo ERROR: nvngx_dlssnr.dll is missing. Re-run DLSS5oneclick installation first.
    exit /b 1
)
if not exist "!PCSX2_OUT!nvngx_dlss.dll" (
    echo ERROR: nvngx_dlss.dll is missing. Re-run DLSS5oneclick installation first.
    exit /b 1
)
if not exist "!PCSX2_OUT!standalone-dlssnr.addon64" exit /b 1
if not exist "!PCSX2_OUT!nvngx.dll" exit /b 1

echo.
echo Cleaning stale DLSS5-Feeder preset entry...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$p='!PCSX2_OUT!ReShadePreset.ini'; if(Test-Path -LiteralPath $p){$s=Get-Content -LiteralPath $p -Raw; $s=$s -replace '(?m)(^|,?)DLSS5_Feed@DLSS5_Feed\.fx(,|$)', { param($m) if($m.Value.StartsWith(',') -and $m.Value.EndsWith(',')){','} else {''} }; Set-Content -LiteralPath $p -Value $s -Encoding ASCII }"

echo Standalone AIO runtime installed.
goto done

:probe_fail
echo ERROR: Could not build the temporary DX11 installer target.
del /q "!PROBE_CPP!" "!PROBE_OBJ!" >nul 2>nul
exit /b 1

:oneclick_check_fail
echo ERROR: DLSS5oneclick rejected the temporary DX11 target.
del /q "!PROBE_CPP!" "!PROBE_OBJ!" "!PROBE_EXE!" >nul 2>nul
exit /b 1

:done
echo.
echo ============================================================
echo DONE
echo KFCSX2: !PCSX2_EXE!
echo IMPORTANT: launch THIS runtime executable, not build-dlss5\kfcsx2-qt\Release\KFCSX2.exe.
echo Renderer: select Direct3D 11 in KFCSX2.
echo Home: opens ReShade.
echo Add-ons: enable Standalone DLSS-NR + SR.
echo Add-ons ^> Neural Rendering: enable VORT motion integration ^(experimental^).
echo Home: leave vort_MotionEffects and DLSS5_AIO_Feed unchecked; the addon schedules them itself.
echo Required driver: 616.64 or newer.
echo F10: processed/original comparison in the standalone addon.
echo ============================================================
exit /b 0
