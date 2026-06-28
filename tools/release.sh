#!/usr/bin/env bash

set -euo pipefail

RELEASE_DIR="bin"
OUTPUT_DIR="release"
SKIP_BUILD=false
WEB=false

usage() {
    cat <<EOF
Usage: $0 <version> [options]

Arguments:
  version              Version (e.g. v0.1.3 or v0.1.3-beta.1)

Options:
  --release-dir DIR    Release directory (default: build)
  --output-dir DIR     Output directory (default: release)
  --skip-build         Skip building
  --web                Build web release
  -h, --help           Show this help
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

VERSION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release-dir)
            RELEASE_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --web)
            WEB=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            if [[ -z "$VERSION" ]]; then
                VERSION="$1"
            else
                echo "Unexpected argument: $1"
                exit 1
            fi
            shift
            ;;
    esac
done

if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.-]+)?$ ]]; then
    echo "Version must look like v0.1.3 or v0.1.3-beta.1"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RELEASE_PATH="$REPO_ROOT/$RELEASE_DIR"
OUTPUT_PATH="$REPO_ROOT/$OUTPUT_DIR"
ARCHIVE_NAME="pilang-$VERSION.zip"
ARCHIVE_PATH="$OUTPUT_PATH/$ARCHIVE_NAME"

mkdir -p "$RELEASE_PATH"
mkdir -p "$OUTPUT_PATH"

cd "$REPO_ROOT"

if ! $SKIP_BUILD; then
    echo "Building native release..."
    make release

    if $WEB; then
        echo "Building web release..."
        make web
    fi
fi

folders_to_refresh=(
    docs
    editors
    imgs
    libs
    samples
    test
)

for folder in "${folders_to_refresh[@]}"; do
    source="$REPO_ROOT/$folder"
    destination="$RELEASE_PATH/$folder"

    if [[ ! -d "$source" ]]; then
        echo "Warning: Skipping missing folder: $folder"
        continue
    fi

    rm -rf "$destination"

    echo "Copying $folder..."
    cp -R "$source" "$destination"
done

root_files=(
    README.md
    LICENSE
    pi.ico
)

for file in "${root_files[@]}"; do
    if [[ -f "$REPO_ROOT/$file" ]]; then
        cp -f "$REPO_ROOT/$file" "$RELEASE_PATH/"
    fi
done

required_files=(
    pilang.exe
    SDL2.dll
    SDL2_image.dll
    SDL2_mixer.dll
    SDL2_ttf.dll
    VeraMono.ttf
)

for file in "${required_files[@]}"; do
    if [[ ! -e "$RELEASE_PATH/$file" ]]; then
        echo "Warning: Expected release file is missing: $file"
    fi
done

rm -f "$ARCHIVE_PATH"

echo "Creating $ARCHIVE_NAME..."

(
    cd "$RELEASE_PATH"
    zip -r -9 "$ARCHIVE_PATH" .
)

for folder in "${folders_to_refresh[@]}"; do
    rm -rf "$RELEASE_PATH/$folder"
done

echo "Release package created: $ARCHIVE_PATH"