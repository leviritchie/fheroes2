#!/usr/bin/env bash

###########################################################################
#   fheroes2: https://github.com/ihhub/fheroes2                           #
#   Copyright (C) 2025                                                    #
#                                                                         #
#   This program is free software; you can redistribute it and/or modify  #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 2 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   This program is distributed in the hope that it will be useful,       #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program; if not, write to the                         #
#   Free Software Foundation, Inc.,                                       #
#   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             #
###########################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IOS_DIR="$REPO_ROOT/ios"
TMP_DIR="$(mktemp -d)"
FORCE_INSTALL=0

if [[ "${1:-}" == "--force" ]]; then
    FORCE_INSTALL=1
fi

trap 'rm -rf "$TMP_DIR"' EXIT

download_package() {
    local package_url="$1"
    local package_file="$2"

    if [[ -n "$(command -v wget)" ]]; then
        wget -O "$TMP_DIR/$package_file" "$package_url"
    elif [[ -n "$(command -v curl)" ]]; then
        curl -o "$TMP_DIR/$package_file" -L "$package_url"
    else
        echo "Neither wget nor curl were found in your system. Unable to download the package archive. Installation aborted."
        exit 1
    fi
}

verify_package() {
    local package_file="$1"
    local package_sha256="$2"

    echo "$package_sha256 *$package_file" > "$TMP_DIR/checksums"

    if [[ -n "$(command -v shasum)" ]]; then
        (cd "$TMP_DIR" && shasum --check --algorithm 256 checksums)
    elif [[ -n "$(command -v sha256sum)" ]]; then
        (cd "$TMP_DIR" && sha256sum --check --strict checksums)
    else
        echo "Neither shasum nor sha256sum were found in your system. Unable to verify the downloaded file. Installation aborted."
        exit 1
    fi
}

install_package() {
    local package_name="$1"
    local package_file="$2"
    local package_sha256="$3"
    local package_url="$4"
    local extracted_name="$5"
    local target_name="$6"
    local target_dir="$IOS_DIR/$target_name"

    if [[ -d "$target_dir" ]]; then
        if [[ "$FORCE_INSTALL" -eq 0 ]]; then
            echo "$target_dir already exists. Use --force to reinstall it."
            return
        fi

        rm -rf "$target_dir"
    fi

    rm -rf "$IOS_DIR/$extracted_name"

    download_package "$package_url" "$package_file"
    verify_package "$package_file" "$package_sha256"

    unzip -q -d "$IOS_DIR" "$TMP_DIR/$package_file"
    mv "$IOS_DIR/$extracted_name" "$target_dir"
}

# We might need to move packages into https://github.com/fheroes2/fheroes2-prebuilt-deps
# repository as we did for other packages.
install_package \
    "release-2.32.10" \
    "release-2.32.10.zip" \
    "7a3c207b8509edc487d658df357ad764cd852d68fe248d307b25c0741d52fdf0" \
    "https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.zip" \
    "SDL-release-2.32.10" \
    "SDL2"

install_package \
    "release-2.8.1" \
    "release-2.8.1.zip" \
    "3738827df73c86268dfa52898780769d1a796316d73b535e2ab5ff2d8d0ff44f" \
    "https://github.com/libsdl-org/SDL_mixer/archive/refs/tags/release-2.8.1.zip" \
    "SDL_mixer-release-2.8.1" \
    "SDL2_mixer"

# Patch SDL_mixer project.
# It is needed since we are trying to build SDL_mixer using the latest SDL2 version.
# Also, SDL_mixer doesn't support iPhone Simulator so we have to change this code or another one.
SDL_MIXER_PROJECT="$IOS_DIR/SDL2_mixer/Xcode/SDL_mixer.xcodeproj/project.pbxproj"
if [[ ! -f "$SDL_MIXER_PROJECT" ]]; then
    echo "Missing SDL_mixer Xcode project: $SDL_MIXER_PROJECT"
    exit 1
fi

sed -i '' 's#$(SRCROOT)/$(PLATFORM)/SDL2.framework/Headers#$(SRCROOT)/../../SDL2/include#' "$SDL_MIXER_PROJECT"
