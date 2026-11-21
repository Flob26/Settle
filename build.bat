@echo off
rem Settle - build script. Double-click to build. Output and errors go to build.log (Claude reads it).
setlocal
cd /d "%~dp0"
call :main > build.log 2>&1
set RC=%ERRORLEVEL%
type build.log
if %RC%==0 (echo. & echo BUILD OK - Settle.dll copied to the "Settle" MO2 mod) else (echo. & echo BUILD FAILED - tell Claude, it can read build.log)
pause
exit /b %RC%

:main
if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\vcpkg"
echo VCPKG_ROOT=%VCPKG_ROOT%
if not exist "extern\CommonLibSSE\CMakeLists.txt" (
  echo Cloning CommonLibSSE-NG...
  git clone --filter=blob:none --branch ng https://github.com/alandtse/CommonLibVR.git extern\CommonLibSSE || exit /b 1
  git -C extern\CommonLibSSE checkout 909517b || exit /b 1
)
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md || exit /b 1
cmake --build build --config Release || exit /b 1
set "OUT=..\..\Flobrim\mods\Settle - Seat Collision for SMP Cloth\SKSE\Plugins"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "build\Release\Settle.dll" "%OUT%\" || exit /b 1
if exist "build\Release\Settle.pdb" copy /y "build\Release\Settle.pdb" "%OUT%\"
exit /b 0
