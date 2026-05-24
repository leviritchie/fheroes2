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

# The upstream SDL_mixer Xcode target compiles music_timidity.c, but it does
# not enable the Timidity MIDI backend or include the bundled Timidity sources.
# fheroes2's bundled Heroes II music falls back to MIDI from HEROES2.AGG, so iOS
# packages need this backend built into SDL2_mixer.framework.
python3 - "$SDL_MIXER_PROJECT" <<'PY'
import sys
from pathlib import Path

project_path = Path(sys.argv[1])
text = project_path.read_text()

def replace_once(content, old, new, description):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"Expected one {description} anchor in {project_path}, found {count}.")
    return content.replace(old, new, 1)

def replace_first(content, old, new, description):
    if old not in content:
        raise SystemExit(f"Missing {description} anchor in {project_path}.")
    return content.replace(old, new, 1)

def ensure_one(content, target, anchor, insertion, description):
    if content.count(target) == 1:
        return content
    if content.count(target) > 1:
        raise SystemExit(f"Expected at most one {description} target in {project_path}, found {content.count(target)}.")
    return replace_once(content, anchor, insertion, description)

timidity_sources = [
    ("A10000000000000000000001", "A20000000000000000000001", "A30000000000000000000001", "common.c"),
    ("A10000000000000000000002", "A20000000000000000000002", "A30000000000000000000002", "instrum.c"),
    ("A10000000000000000000003", "A20000000000000000000003", "A30000000000000000000003", "mix.c"),
    ("A10000000000000000000004", "A20000000000000000000004", "A30000000000000000000004", "output.c"),
    ("A10000000000000000000005", "A20000000000000000000005", "A30000000000000000000005", "playmidi.c"),
    ("A10000000000000000000006", "A20000000000000000000006", "A30000000000000000000006", "readmidi.c"),
    ("A10000000000000000000007", "A20000000000000000000007", "A30000000000000000000007", "resample.c"),
    ("A10000000000000000000008", "A20000000000000000000008", "A30000000000000000000008", "tables.c"),
    ("A10000000000000000000009", "A20000000000000000000009", "A30000000000000000000009", "timidity.c"),
]

macro_anchor = """\t\t\t\t\tMUSIC_WAV,
\t\t\t\t\t"$(CONFIG_PREPROCESSOR_DEFINITIONS)","""
macro_insertion = """\t\t\t\t\tMUSIC_WAV,
\t\t\t\t\tMUSIC_MID,
\t\t\t\t\tMUSIC_MID_TIMIDITY,
\t\t\t\t\t"$(CONFIG_PREPROCESSOR_DEFINITIONS)","""
patched_macros = """\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (
\t\t\t\t\tMUSIC_FLAC_DRFLAC,
\t\t\t\t\tMUSIC_MP3_MINIMP3,
\t\t\t\t\tMUSIC_OGG,
\t\t\t\t\tOGG_USE_STB,
\t\t\t\t\tMUSIC_WAV,
\t\t\t\t\tMUSIC_MID,
\t\t\t\t\tMUSIC_MID_TIMIDITY,
\t\t\t\t\t"$(CONFIG_PREPROCESSOR_DEFINITIONS)",
\t\t\t\t);"""

while text.count(patched_macros) < 2:
    text = replace_first(text, macro_anchor, macro_insertion, "unpatched SDL_mixer preprocessor block")
if text.count(patched_macros) != 2:
    raise SystemExit(f"Expected two patched SDL_mixer preprocessor blocks in {project_path}, found {text.count(patched_macros)}.")

for framework_id, file_id, static_id, name in timidity_sources:
    text = ensure_one(
        text,
        f"{framework_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};",
        "/* End PBXBuildFile section */",
        f"\t\t{framework_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};\n/* End PBXBuildFile section */",
        f"Framework PBXBuildFile for {name}",
    )
    text = ensure_one(
        text,
        f"{static_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};",
        "/* End PBXBuildFile section */",
        f"\t\t{static_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};\n/* End PBXBuildFile section */",
        f"Static Library PBXBuildFile for {name}",
    )
    text = ensure_one(
        text,
        f"{file_id} /* {name} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = sourcecode.c.c; path = ../src/codecs/timidity/{name}; sourceTree = SOURCE_ROOT; }};",
        "/* End PBXFileReference section */",
        f"\t\t{file_id} /* {name} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = sourcecode.c.c; path = ../src/codecs/timidity/{name}; sourceTree = SOURCE_ROOT; }};\n/* End PBXFileReference section */",
        f"PBXFileReference for {name}",
    )
    text = ensure_one(
        text,
        f"\t\t\t\t{framework_id} /* {name} in Sources */,\n",
        "\t\t\t\tAAE405FB1F9607C300EDAF53 /* music_timidity.c in Sources */,\n",
        f"\t\t\t\tAAE405FB1F9607C300EDAF53 /* music_timidity.c in Sources */,\n\t\t\t\t{framework_id} /* {name} in Sources */,\n",
        f"Framework source phase entry for {name}",
    )
    text = ensure_one(
        text,
        f"\t\t\t\t{static_id} /* {name} in Sources */,\n",
        "\t\t\t\tF38233552731961A00F7F527 /* music_timidity.c in Sources */,\n",
        f"\t\t\t\tF38233552731961A00F7F527 /* music_timidity.c in Sources */,\n\t\t\t\t{static_id} /* {name} in Sources */,\n",
        f"Static Library source phase entry for {name}",
    )

missing_sources = []
for framework_id, file_id, static_id, name in timidity_sources:
    expected_entries = [
        f"{framework_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};",
        f"{static_id} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_id} /* {name} */; }};",
        f"{file_id} /* {name} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = sourcecode.c.c; path = ../src/codecs/timidity/{name}; sourceTree = SOURCE_ROOT; }};",
        f"\t\t\t\t{framework_id} /* {name} in Sources */,\n",
        f"\t\t\t\t{static_id} /* {name} in Sources */,\n",
    ]
    if any(text.count(entry) != 1 for entry in expected_entries):
        missing_sources.append(name)

missing_tokens = [] if text.count(patched_macros) == 2 else ["MUSIC_MID/MUSIC_MID_TIMIDITY preprocessor blocks"]
duplicate_sources = [
    name
    for _, file_id, _, name in timidity_sources
    if text.count(f"{file_id} /* {name} */") != 3
]
if missing_tokens or missing_sources or duplicate_sources:
    raise SystemExit(
        "Failed to patch SDL_mixer Timidity MIDI support. "
        f"Missing tokens: {', '.join(missing_tokens) or 'none'}; "
        f"missing sources: {', '.join(missing_sources) or 'none'}; "
        f"duplicate sources: {', '.join(duplicate_sources) or 'none'}."
    )

project_path.write_text(text)
PY
