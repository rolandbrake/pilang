param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$Prefix
)

$ErrorActionPreference = 'Stop'

function Invoke-Native {
    param([string]$Command, [string[]]$Arguments)

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

function Ensure-Source {
    param([string]$Name, [string]$Repository, [string]$Tag)

    $source = Join-Path $thirdParty $Name
    if (Test-Path $source) {
        Write-Host "Using existing $Name source at $source"
        return $source
    }

    Invoke-Native git @('clone', '--depth', '1', '--branch', $Tag, $Repository, $source)
    return $source
}

function Build-Dependency {
    param([string]$Name, [string]$Source, [string[]]$Options)

    $build = Join-Path $buildRoot $Name
    Invoke-Native cmake (@('-S', $Source, '-B', $build, '-G', 'MinGW Makefiles',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$prefix",
        "-DCMAKE_INSTALL_PREFIX=$prefix") + $Options)
    Invoke-Native cmake @('--build', $build, '--parallel', '1')
    Invoke-Native cmake @('--install', $build)
}

foreach ($command in 'git', 'cmake') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command '$command' was not found in PATH."
    }
}

$thirdParty = Join-Path $Root 'third_party'
$buildRoot = Join-Path $thirdParty 'build'
if (-not $Prefix) {
    $Prefix = Join-Path $Root 'resources/windows-x64-static'
}
$prefix = $Prefix
New-Item -ItemType Directory -Force -Path $thirdParty, $buildRoot, $prefix | Out-Null

$sdl = Ensure-Source 'SDL2' 'https://github.com/libsdl-org/SDL.git' 'release-2.32.8'
Build-Dependency 'SDL2' $sdl @('-DSDL_SHARED=OFF', '-DSDL_STATIC=ON')

$ttf = Ensure-Source 'SDL2_ttf' 'https://github.com/libsdl-org/SDL_ttf.git' 'release-2.24.0'
Invoke-Native git @('-C', $ttf, 'submodule', 'update', '--init', '--depth', '1', 'external/freetype')
Build-Dependency 'SDL2_ttf' $ttf @('-DBUILD_SHARED_LIBS=OFF', '-DSDL2_SHARED=OFF',
    '-DSDL2TTF_VENDORED=ON', '-DSDL2TTF_HARFBUZZ=OFF')

$image = Ensure-Source 'SDL2_image' 'https://github.com/libsdl-org/SDL_image.git' 'release-2.8.8'
Build-Dependency 'SDL2_image' $image @('-DBUILD_SHARED_LIBS=OFF', '-DSDL2IMAGE_VENDORED=ON',
    '-DSDL2IMAGE_BACKEND_STB=ON', '-DSDL2IMAGE_AVIF=OFF', '-DSDL2IMAGE_JXL=OFF',
    '-DSDL2IMAGE_TIF=OFF', '-DSDL2IMAGE_WEBP=OFF')

$mixer = Ensure-Source 'SDL2_mixer' 'https://github.com/libsdl-org/SDL_mixer.git' 'release-2.8.1'
Build-Dependency 'SDL2_mixer' $mixer @('-DBUILD_SHARED_LIBS=OFF', '-DSDL2MIXER_VENDORED=OFF',
    '-DSDL2MIXER_FLAC=ON', '-DSDL2MIXER_FLAC_DRFLAC=ON', '-DSDL2MIXER_FLAC_LIBFLAC=OFF',
    '-DSDL2MIXER_MP3=ON', '-DSDL2MIXER_MP3_MINIMP3=ON', '-DSDL2MIXER_MP3_MPG123=OFF',
    '-DSDL2MIXER_VORBIS=STB', '-DSDL2MIXER_OPUS=OFF', '-DSDL2MIXER_MIDI=OFF',
    '-DSDL2MIXER_MOD=OFF', '-DSDL2MIXER_WAVPACK=OFF')

foreach ($library in 'libSDL2.a', 'libSDL2_ttf.a', 'libSDL2_image.a', 'libSDL2_mixer.a') {
    $path = Join-Path $prefix "lib/$library"
    if (-not (Test-Path $path)) {
        throw "Expected static library was not installed: $path"
    }
}

Write-Host "Static dependencies are ready in $prefix"
