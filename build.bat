@echo off
REM Cross-platform build script for AudSync (Windows)

echo Building AudSync for Windows...

REM Create build directory if it doesn't exist
if not exist "build" (
    mkdir build
)

cd build

REM Configure with CMake
echo Configuring with CMake...
if defined VCPKG_ROOT (
    echo Using vcpkg toolchain...
    cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
) else (
    echo Warning: VCPKG_ROOT not set. Using default cmake configuration.
    cmake ..
)

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

REM Build the project
echo Building project...
cmake --build . --config Release

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build completed successfully!
    echo.
    echo Executables created:
    echo   %cd%\Release\audsync_server.exe - Audio streaming server
    echo   %cd%\Release\audsync_client.exe - Audio streaming client
) else (
    echo Build failed!
)

pause
