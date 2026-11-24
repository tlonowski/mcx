#!/usr/bin/env zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_HPP="$ROOT_DIR/MCommander/Core/Version.hpp"
XCPROJ="$ROOT_DIR/MCommander.xcodeproj"
PBXPROJ="$XCPROJ/project.xcproj"

CURRENT_BUILD=$(grep -E '#define MCX_BUILD' "$VERSION_HPP" | sed -E 's/.*"([0-9]+)".*/\1/')
NEXT_BUILD=$((CURRENT_BUILD + 1))
BASE_VER=$(grep -E '#define MCX_VERSION' "$VERSION_HPP" | sed -E 's/.*"([^"]+)".*/\1/')
FULL_VER="${BASE_VER}.${NEXT_BUILD}"

echo "Building mcx v${FULL_VER} (Release)..."

sed -i '' -E "s/#define MCX_BUILD[[:space:]]+\"[0-9]+\"/#define MCX_BUILD        \"${NEXT_BUILD}\"/" "$VERSION_HPP"
sed -i '' -E "s/\"CURRENT_PROJECT_VERSION\": \"[0-9]+\"/\"CURRENT_PROJECT_VERSION\": \"${NEXT_BUILD}\"/" "$PBXPROJ"
sed -i '' -E "s/\"MARKETING_VERSION\": \"[^\"]+\"/\"MARKETING_VERSION\": \"${FULL_VER}\"/" "$PBXPROJ"

xcodebuild -project "$XCPROJ" -scheme MCommander -configuration Release build | tail -n 20

APP_SRC="$HOME/Library/Developer/Xcode/DerivedData/MCommander-ermfzgvzkzccwuhfgnbdvmscrwon/Build/Products/Release/mcx.app"
if [[ -d "$APP_SRC" ]]; then
    pkill -x mcx || true
    sleep 0.3
    rm -rf /Applications/mcx.app
    ditto "$APP_SRC" /Applications/mcx.app
    open /Applications/mcx.app
    echo "Installed and launched /Applications/mcx.app (v${FULL_VER})"
fi
