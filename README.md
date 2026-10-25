# blockwork
This is where she makes a mod.

<img src="logo.png" width="150" alt="the mod's logo" />

*Update logo.png to change your mod's icon (please)*

## Getting started
We recommend heading over to [the getting started section on our docs](https://docs.geode-sdk.org/getting-started/) for useful info on what to do next.

## Build instructions
### Local macOS development

The local environment uses Geode SDK **v5.11.2**, bindings commit
`1605f500` (matching that release), and Geometry Dash **2.2081**.
The SDK, bindings, and dependency cache live in the ignored `.tools/` directory.
The Geode CLI is installed in `~/.local/bin`.

```sh
source scripts/dev-env.sh
MACOSX_DEPLOYMENT_TARGET=13.0 cargo build -p blockwork-ffi --release --target aarch64-apple-darwin --manifest-path ../Blockwork/Cargo.toml
cmake --preset mac-dev
cmake --build --preset mac-dev
```

The `mac-dev` preset links the Rust engine from the sibling `../Blockwork`
checkout, including its macOS backend fixes. Rebuild that library when the engine changes.
The older `.tools/blockwork-engine` checkout is retained but is no longer the preset’s source. The preset builds
for Apple Silicon with debug symbols and generates
`build/mac-dev/compile_commands.json` for editor code completion. Open this
directory as a CMake project in CLion and select the `mac-dev` preset.
The build packages the mod and installs it into the CLI's active GD profile.
Restart Geometry Dash to load a rebuilt mod.

```sh
geode profile list
geode run
```

The official `geode.devtools` mod is also installed in the local game; restart
the game to load it. Its tools can be opened with **F10** (or **Fn+F10**, depending
on your keyboard settings).

To recreate the SDK checkout on another Mac:

```sh
mkdir -p .tools
git clone --branch v5.11.2 --depth 1 https://github.com/geode-sdk/geode .tools/geode-sdk
git clone https://github.com/geode-sdk/bindings .tools/bindings
git -C .tools/bindings checkout 1605f500
git clone https://github.com/Blockworked/Blockwork ../Blockwork
git -C ../Blockwork checkout e285eac69af1759811b39aeedd861653bd06a30b
source scripts/dev-env.sh
geode sdk install-binaries
geode profile add "$HOME/Library/Application Support/Steam/steamapps/common/Geometry Dash/Geometry Dash.app" --name gd-2.2081 mac
```

Keep this SDK pinned while using the downgraded game. The existing `build.sh`
is an older Windows cross-compilation script with machine-specific paths;
use the presets above for local macOS work.

### Other platforms

For more info, see [our docs](https://docs.geode-sdk.org/getting-started/create-mod#build)
```sh
# Assuming you have the Geode CLI set up already
geode build
```

# Resources
* [Geode SDK Documentation](https://docs.geode-sdk.org/)
* [Geode SDK Source Code](https://github.com/geode-sdk/geode/)
* [Geode CLI](https://github.com/geode-sdk/cli)
* [Bindings](https://github.com/geode-sdk/bindings/)
* [Dev Tools](https://github.com/geode-sdk/DevTools)

### Native macOS integration tests

Build and install the mod first, close Geometry Dash and the Blockwork daemon,
then run:

```sh
python3 scripts/autotest-macos.py --level 'Bloodbath Infinity 2' --runs 1 --speed 1 --output /tmp/blockwork-gd-normal
python3 scripts/autotest-macos.py --level 'Bloodbath Infinity 2' --runs 10 --speed 10 --output /tmp/blockwork-gd-10x
```

Each attempt freshly imports `~/Downloads/ChesszDC-Bloodbath-Infinity-zBot.gdr`
inside the game, links that new macro to the requested local level, and verifies
its identity in the terminal result. The runner launches through Steam: direct
Geode CLI launches exited before the menu on this installation. It temporarily
enables QOL safe mode, disables noclip and unrelated mods, sets the requested QOL
speed, and restores configuration files after stopping its game process. Existing
game saves are backed up. The output directory must be new; it contains replay
hashes, fresh macro copies, configuration backups, logs and result manifests.
A failed attempt stops the batch. Passing a finite batch is evidence for that
batch, not a guarantee about all future hardware and mod configurations.

On macOS, the game inlines `processCommands` into its update loop. Playback and
step recording therefore use `processQueuedButtons`, the called physics-step
boundary. Other platforms retain the existing `processCommands` hook.

CI builds the pinned Blockwork FFI library before building the mod. macOS uses a
universal arm64/x86_64 archive; Windows uses the MSVC static library. Both the
non-publishing checks and the release workflow pass the generated library and
header paths to CMake. The release workflow remains manually dispatched.
