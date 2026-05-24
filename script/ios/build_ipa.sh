#!/usr/bin/env bash

###########################################################################
#   fheroes2: https://github.com/ihhub/fheroes2                           #
#   Copyright (C) 2026                                                    #
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

PROJECT_PATH="$IOS_DIR/fheroes2.xcodeproj"
SCHEME="${FHEROES2_IOS_SCHEME:-fheroes2}"
CONFIGURATION="${FHEROES2_IOS_CONFIGURATION:-Release}"
BUILD_DIR="${FHEROES2_IOS_BUILD_DIR:-$IOS_DIR/build}"
ARCHIVE_PATH="${FHEROES2_IOS_ARCHIVE_PATH:-$BUILD_DIR/fheroes2.xcarchive}"
IPA_STAGING_DIR="$BUILD_DIR/ipa"
IPA_PATH="${FHEROES2_IOS_IPA_PATH:-$BUILD_DIR/fheroes2.ipa}"
BUNDLE_IDENTIFIER="${FHEROES2_IOS_BUNDLE_IDENTIFIER:-io.github.leviritchie.fheroes2}"
DEVELOPMENT_TEAM_VALUE="${FHEROES2_IOS_DEVELOPMENT_TEAM:-${DEVELOPMENT_TEAM:-}}"
INCLUDE_DEMO_DATA="${FHEROES2_IOS_INCLUDE_DEMO_DATA:-1}"
DEFAULT_BUNDLE_IDENTIFIER="io.github.leviritchie.fheroes2"

is_truthy() {
    case "$1" in
        1|true|TRUE|yes|YES|on|ON)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

first_file_in_dir() {
    local dir="$1"
    local file_count

    if [[ ! -d "$dir" ]]; then
        return 1
    fi

    file_count="$(find "$dir" -type f -print | wc -l | tr -d '[:space:]')"
    [[ "$file_count" != "0" ]]
}

require_macos() {
    if [[ "$(uname 2> /dev/null)" != "Darwin" ]]; then
        echo "iOS IPA packaging requires macOS with Xcode and the iOS SDK installed."
        exit 1
    fi

    if [[ -z "$(command -v xcodebuild)" ]]; then
        echo "xcodebuild was not found. Install Xcode and its iOS SDK before packaging the IPA."
        exit 1
    fi

    if [[ -z "$(command -v zip)" ]]; then
        echo "zip was not found. Install zip before packaging the IPA."
        exit 1
    fi
}

ensure_ios_dependencies() {
    "$SCRIPT_DIR/install_packages.sh"
}

prepare_game_data() {
    if ! first_file_in_dir "$REPO_ROOT/data"; then
        if is_truthy "$INCLUDE_DEMO_DATA"; then
            bash "$REPO_ROOT/script/demo/download_demo_version.sh" "$REPO_ROOT"
        else
            echo "No game data was found in $REPO_ROOT/data."
            echo "Copy Heroes II data into the repository root or set FHEROES2_IOS_INCLUDE_DEMO_DATA=1 to bundle the demo data."
            exit 1
        fi
    fi

    mkdir -p "$REPO_ROOT/anim" "$REPO_ROOT/data" "$REPO_ROOT/maps" "$REPO_ROOT/music"
}

archive_app() {
    local signing_args=(
        CODE_SIGNING_ALLOWED=NO
        CODE_SIGNING_REQUIRED=NO
        CODE_SIGN_IDENTITY=
    )

    if [[ -n "$DEVELOPMENT_TEAM_VALUE" ]]; then
        signing_args=(
            CODE_SIGNING_ALLOWED=YES
            CODE_SIGNING_REQUIRED=YES
            CODE_SIGN_STYLE=Automatic
            DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM_VALUE"
        )
    fi

    xcodebuild \
        -project "$PROJECT_PATH" \
        -scheme "$SCHEME" \
        -configuration "$CONFIGURATION" \
        -sdk iphoneos \
        -destination "generic/platform=iOS" \
        archive \
        -archivePath "$ARCHIVE_PATH" \
        EXCLUDED_SOURCE_FILE_NAMES="SDL2/src/misc/macosx/*" \
        OTHER_LDFLAGS="-framework AudioToolbox -framework CoreAudio -framework CoreFoundation -framework CoreVideo -weak_framework GameController -weak_framework CoreHaptics -weak_framework Metal -weak_framework QuartzCore" \
        "${signing_args[@]}"
}

package_ipa() {
    local -a app_candidates
    local app_dir

    shopt -s nullglob
    app_candidates=("$ARCHIVE_PATH/Products/Applications"/*.app)
    shopt -u nullglob

    if [[ "${#app_candidates[@]}" -eq 0 ]]; then
        echo "No .app bundle was found in $ARCHIVE_PATH/Products/Applications."
        exit 1
    fi
    app_dir="${app_candidates[0]}"

    if [[ -n "$BUNDLE_IDENTIFIER" ]]; then
        if [[ -n "$DEVELOPMENT_TEAM_VALUE" ]]; then
            if [[ "$BUNDLE_IDENTIFIER" != "$DEFAULT_BUNDLE_IDENTIFIER" ]]; then
                echo "FHEROES2_IOS_BUNDLE_IDENTIFIER cannot be changed after an Xcode-signed archive without invalidating the signature."
                echo "For signed builds, set the bundle identifier in the Xcode project or build unsigned for external signing."
                exit 1
            fi
        else
            /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $BUNDLE_IDENTIFIER" "$app_dir/Info.plist"
        fi
    fi

    rm -rf "$IPA_STAGING_DIR"
    mkdir -p "$IPA_STAGING_DIR/Payload"
    cp -R "$app_dir" "$IPA_STAGING_DIR/Payload/"

    rm -f "$IPA_PATH"
    (cd "$IPA_STAGING_DIR" && zip -qry "$IPA_PATH" Payload)

    echo "Created IPA: $IPA_PATH"
}

require_macos
ensure_ios_dependencies
prepare_game_data
archive_app
package_ipa
