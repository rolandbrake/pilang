param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Version,

    [switch]$SkipBuild,
    [switch]$Web
)

$ErrorActionPreference = "Stop"

if ($Version -notmatch '^v\d+\.\d+\.\d+(-[A-Za-z0-9.-]+)?$') {
    throw "Version must look like v0.1.4 or v0.1.4-beta.1"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$outputPath = Join-Path $repoRoot "release"
$packagePath = Join-Path $outputPath "Pilang-$Version"
$archiveName = "pilang-$Version.zip"
$archivePath = Join-Path $outputPath $archiveName

Push-Location $repoRoot
try {
    New-Item -ItemType Directory -Force $outputPath | Out-Null

    if (-not $SkipBuild) {
        Write-Host "Building native release..."
        make release

        if ($Web) {
            Write-Host "Building web release..."
            make web
        }
    }

    $runtime = Join-Path $repoRoot "bin/pilang.exe"
    if (-not (Test-Path $runtime)) {
        throw "Missing bin/pilang.exe. Run make release first."
    }

    if (Test-Path $packagePath) {
        Remove-Item -LiteralPath $packagePath -Recurse -Force
    }
    New-Item -ItemType Directory -Force (Join-Path $packagePath "bin") | Out-Null
    Copy-Item -LiteralPath $runtime -Destination (Join-Path $packagePath "bin/pilang.exe")

    $foldersToCopy = @(
        "docs",
        "editors",
        "imgs",
        "libs",
        "resources",
        "samples",
        "test",
        "benchmark"
    )

    foreach ($folder in $foldersToCopy) {
        $source = Join-Path $repoRoot $folder
        $destination = Join-Path $packagePath $folder

        if (-not (Test-Path $source)) {
            Write-Warning "Skipping missing folder: $folder"
            continue
        }

        if (Test-Path $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }

        Write-Host "Copying $folder..."
        Copy-Item -LiteralPath $source -Destination $destination -Recurse
    }

    $rootFilesToCopy = @(
        "README.md",
        "LICENSE",
        "pi.ico"
    )

    foreach ($file in $rootFilesToCopy) {
        $source = Join-Path $repoRoot $file
        if (Test-Path $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $packagePath $file) -Force
        }
    }

    $releaseTools = @("run_tests.py", "run_benchmarks.py")
    $toolsPath = Join-Path $packagePath "tools"
    New-Item -ItemType Directory -Force $toolsPath | Out-Null
    foreach ($file in $releaseTools) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "tools/$file") -Destination (Join-Path $toolsPath $file)
    }

    $dynamicLibraries = Get-ChildItem -LiteralPath $packagePath -Recurse -File |
        Where-Object { $_.Extension -in '.dll', '.so', '.dylib' }
    if ($dynamicLibraries) {
        throw "A static release cannot include dynamic libraries: $($dynamicLibraries.Name -join ', ')"
    }

    if (Test-Path $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    Write-Host "Creating $archiveName..."
    Compress-Archive -Path $packagePath -DestinationPath $archivePath -CompressionLevel Optimal

    Write-Host "Release package created: $archivePath"
}
finally {
    Pop-Location
}
