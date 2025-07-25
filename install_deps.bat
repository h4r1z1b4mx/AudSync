@echo off
echo Installing dependencies for AudSync on Windows...

REM Check if vcpkg is available
where vcpkg >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: vcpkg not found in PATH
    echo Please install vcpkg first:
    echo 1. git clone https://github.com/Microsoft/vcpkg.git
    echo 2. cd vcpkg
    echo 3. .\bootstrap-vcpkg.bat
    echo 4. Add vcpkg to your PATH
    echo 5. Run: vcpkg integrate install
    pause
    exit /b 1
)

echo Installing PortAudio...
vcpkg install portaudio

echo.
echo Dependencies installed successfully!
echo.
echo To build the project:
echo 1. mkdir build
echo 2. cd build
echo 3. cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
echo 4. cmake --build .
echo.
pause
