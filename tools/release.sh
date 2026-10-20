#!/usr/bin/env bash

set -euo pipefail

SKIP_BUILD=false
WEB=false

usage() {
    cat <<EOF
Usage: $0 <version> [options]

Arguments:
  version              Version (e.g. v0.1.4 or v0.1.4-beta.1)

Options:
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
    echo "Version must look like v0.1.4 or v0.1.4-beta.1"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

OUTPUT_PATH="$REPO_ROOT/release"
PACKAGE_PATH="$OUTPUT_PATH/Pilang-$VERSION"
ARCHIVE_NAME="pilang-linux-x64-$VERSION.tar.gz"
ARCHIVE_PATH="$OUTPUT_PATH/$ARCHIVE_NAME"

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

runtime="$REPO_ROOT/bin/pilang"
if [[ ! -f "$runtime" ]]; then
    echo "Missing bin/pilang. Run make release first."
    exit 1
fi

rm -rf "$PACKAGE_PATH"
mkdir -p "$PACKAGE_PATH/bin"
cp -f "$runtime" "$PACKAGE_PATH/bin/pilang"

folders_to_copy=(
    docs
    editors
    imgs
    libs
    resources
    samples
    test
    benchmark
)

for folder in "${folders_to_copy[@]}"; do
    source="$REPO_ROOT/$folder"
    destination="$PACKAGE_PATH/$folder"

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
        cp -f "$REPO_ROOT/$file" "$PACKAGE_PATH/"
    fi
done

mkdir -p "$PACKAGE_PATH/tools"
cp -f "$REPO_ROOT/tools/run_tests.py" "$PACKAGE_PATH/tools/run_tests.py"
cp -f "$REPO_ROOT/tools/run_benchmarks.py" "$PACKAGE_PATH/tools/run_benchmarks.py"

mapfile -t dynamic_libraries < <(find "$PACKAGE_PATH" -type f \( -iname '*.so' -o -iname '*.dylib' \))
if [[ ${#dynamic_libraries[@]} -gt 0 ]]; then
    printf 'A static release cannot include dynamic libraries:\n%s\n' "${dynamic_libraries[@]}"
    exit 1
fi

rm -f "$ARCHIVE_PATH"

echo "Creating $ARCHIVE_NAME..."

(
    cd "$OUTPUT_PATH"
    tar -czf "$ARCHIVE_PATH" "Pilang-$VERSION"
)

echo "Release package created: $ARCHIVE_PATH"
