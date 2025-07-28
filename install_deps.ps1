# PowerShell script to install dependencies for AudSync on Windows

Write-Host "Installing dependencies for AudSync on Windows..." -ForegroundColor Green

# Check if vcpkg is available
$vcpkgPath = Get-Command vcpkg -ErrorAction SilentlyContinue
if (-not $vcpkgPath) {
    Write-Host "ERROR: vcpkg not found in PATH" -ForegroundColor Red
    Write-Host "Please install vcpkg first:" -ForegroundColor Yellow
    Write-Host "1. git clone https://github.com/Microsoft/vcpkg.git" -ForegroundColor White
    Write-Host "2. cd vcpkg" -ForegroundColor White
    Write-Host "3. .\bootstrap-vcpkg.bat" -ForegroundColor White
    Write-Host "4. Add vcpkg to your PATH" -ForegroundColor White
    Write-Host "5. Run: vcpkg integrate install" -ForegroundColor White
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Installing PortAudio..." -ForegroundColor Yellow
& vcpkg install portaudio

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Dependencies installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "To build the project:" -ForegroundColor Yellow
    Write-Host "1. mkdir build" -ForegroundColor White
    Write-Host "2. cd build" -ForegroundColor White
    Write-Host "3. cmake .. -DCMAKE_TOOLCHAIN_FILE=`$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -ForegroundColor White
    Write-Host "4. cmake --build ." -ForegroundColor White
} else {
    Write-Host "Failed to install dependencies!" -ForegroundColor Red
}

Read-Host "Press Enter to exit"
