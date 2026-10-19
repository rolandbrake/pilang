param(
    [switch]$InstallMSYS2
)

$ErrorActionPreference = "Stop"
$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$bash = Join-Path $msysRoot "usr\bin\bash.exe"

if (-not (Test-Path $bash)) {
    if (-not $InstallMSYS2) {
        throw "MSYS2 was not found. Install it from https://www.msys2.org/, then run this script again. To let winget install it, rerun with -InstallMSYS2."
    }

    winget install --exact --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements
    if (-not (Test-Path $bash)) {
        throw "MSYS2 was installed, but $bash was not found. Restart PowerShell and run this script again."
    }
}

$packages = @(
    "mingw-w64-ucrt-x86_64-gcc",
    "mingw-w64-ucrt-x86_64-make",
    "mingw-w64-ucrt-x86_64-pkgconf",
    "mingw-w64-ucrt-x86_64-SDL2",
    "mingw-w64-ucrt-x86_64-SDL2_image",
    "mingw-w64-ucrt-x86_64-SDL2_ttf",
    "mingw-w64-ucrt-x86_64-SDL2_mixer"
)

& $bash -lc "pacman -Sy --needed --noconfirm $($packages -join ' ')"
if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 package installation failed. Open the MSYS2 UCRT64 terminal, run 'pacman -Syu', then rerun this script."
}

Write-Host "Pilang build dependencies are ready. Open the MSYS2 UCRT64 terminal and run:"
Write-Host "  cd /path/to/pilang"
Write-Host "  make release"
