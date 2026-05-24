# Package installation for XCode

Run the `install_packages.sh` and it will try to install everything at once.

# IPA packaging

Run `bash ./script/ios/build_ipa.sh` from the repository root on macOS to create `ios/build/fheroes2.ipa`.

The IPA is unsigned unless `FHEROES2_IOS_DEVELOPMENT_TEAM` or `DEVELOPMENT_TEAM` is set. Unsigned IPAs are intended for
sideloading tools that sign during installation. The script downloads the free Heroes II demo data when the repository does not
already contain game data; set `FHEROES2_IOS_INCLUDE_DEMO_DATA=0` to require local game data instead.
