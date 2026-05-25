# Package installation for XCode

Run the `install_packages.sh` and it will try to install everything at once.

# IPA packaging

Run `bash ./script/ios/build_ipa.sh` from the repository root on macOS to create `ios/build/fheroes2.ipa`.

The IPA is unsigned unless `FHEROES2_IOS_DEVELOPMENT_TEAM` or `DEVELOPMENT_TEAM` is set. Unsigned IPAs are intended for
sideloading tools that sign during installation. The script downloads the free Heroes II demo data when the repository does not
already contain game data; set `FHEROES2_IOS_INCLUDE_DEMO_DATA=0` to require local game data instead.

# Importing full game data on iOS

Do not upload proprietary Heroes II data to GitHub Actions or commit it to this repository. The public IPA can be built with
demo data, then full data can be copied privately into the installed app through iOS Files/Finder file sharing.

Create a `fheroes2` folder in the app's shared Documents area and copy the original game data into this layout:

```text
fheroes2/
  data/
    HEROES2.AGG
    HEROES2X.AGG
  maps/
    *.mp2
    *.mx2
    *.fh2m
  music/
    optional external music
```

The iOS build searches `Documents/fheroes2` and `Documents` before the bundled app resources. `HEROES2.AGG` and
`HEROES2X.AGG` must be in the same imported `data` directory for Price of Loyalty support, Resurrection maps, the editor,
and random map generation to unlock.
