# Agent Notes

## iOS IPA Packaging

- Use `script/ios/build_ipa.sh` as the single entry point for IPA packaging. Keep GitHub workflows thin wrappers around this script so local and CI packaging do not drift.
- A sideloadable IPA must contain `Payload/<AppName>.app` at the archive root. Do not zip the `.app` directly at the IPA root.
- The Xcode project expects generated dependency folders at `ios/SDL2` and `ios/SDL2_mixer`. Keep the SDL_mixer path lowercase to match the project file and case-sensitive filesystems.
- `script/ios/install_packages.sh` must be runnable from any current working directory. Resolve paths through the script location rather than assuming the caller is at the repository root.
- Do not commit generated SDL dependency folders, Xcode build products, IPA files, or proprietary Heroes II game data. The IPA script can bundle the free demo data for personal sideload builds when local game data is absent.
- For signed builds, pass a team ID through `FHEROES2_IOS_DEVELOPMENT_TEAM` or `DEVELOPMENT_TEAM`. Without a team ID, the build script intentionally produces an unsigned IPA for tools that sign during sideload installation.
- When adding shell scripts from Windows or another checkout with `core.filemode=false`, either stage executable mode explicitly or invoke them through `bash` in CI workflows. Direct `./script.sh` workflow calls can fail on macOS if the executable bit is not committed.
- Keep the shared `fheroes2.xcscheme` explicit about SDL dependencies. SDL has multiple products named `SDL2.framework`; if Xcode uses implicit dependency resolution it can select the macOS `Framework` target during an `iphoneos` archive and fail on missing `Carbon.framework`.
