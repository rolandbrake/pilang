param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Version,

    [string]$ReleaseDir = "bin",
    [string]$OutputDir = "release",
    [switch]$SkipBuild,
    [switch]$Web
)

$ErrorActionPreference = "Stop"

if ($Version -notmatch '^v\d+\.\d+\.\d+(-[A-Za-z0-9.-]+)?$') {
    throw "Version must look like v0.1.3 or v0.1.3-beta.1"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$releasePath = Join-Path $repoRoot $ReleaseDir
$outputPath = Join-Path $repoRoot $OutputDir
$archiveName = "pilang-$Version.zip"
$archivePath = Join-Path $outputPath $archiveName

Push-Location $repoRoot
try {
    New-Item -ItemType Directory -Force $releasePath | Out-Null
    New-Item -ItemType Directory -Force $outputPath | Out-Null

    if (-not $SkipBuild) {
        Write-Host "Building native release..."
        make release

        if ($Web) {
            Write-Host "Building web release..."
            make web
        }
    }

    $foldersToRefresh = @(
        "docs",
        "editors",
        "imgs",
        "libs",
        "samples",
        "test"
    )

    foreach ($folder in $foldersToRefresh) {
        $source = Join-Path $repoRoot $folder
        $destination = Join-Path $releasePath $folder

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
            Copy-Item -LiteralPath $source -Destination (Join-Path $releasePath $file) -Force
        }
    }

    $requiredFiles = @(
        "pilang.exe",
        "SDL2.dll",
        "SDL2_image.dll",
        "SDL2_mixer.dll",
        "SDL2_ttf.dll",
        "VeraMono.ttf"
    )

    foreach ($file in $requiredFiles) {
        if (-not (Test-Path (Join-Path $releasePath $file))) {
            Write-Warning "Expected release file is missing: $file"
        }
    }

    if (Test-Path $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    Write-Host "Creating $archiveName..."
    Compress-Archive -Path (Join-Path $releasePath "*") -DestinationPath $archivePath -CompressionLevel Optimal

    foreach ($folder in $foldersToRefresh) {
        $destination = Join-Path $releasePath $folder
        if (Test-Path $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
    }

    Write-Host "Release package created: $archivePath"
}
finally {
    Pop-Location
}
