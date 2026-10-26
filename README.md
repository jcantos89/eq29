# EQ29 — My Dynamic EQ (LV2 + VST2)

A stereo LV2 and VST2 dynamic equalizer built with [DPF](https://github.com/DISTRHO/DPF)
and its official [Dear ImGui integration](https://github.com/DISTRHO/DPF-Widgets).
Four bands are active initially; eight fixed slots are available. Bell, low-shelf,
and high-shelf filters each have a band-local, stereo-linked peak detector.

## Build on Linux

Install a C++17 compiler, CMake 3.20+, Make, pkg-config, X11 and OpenGL headers.
On Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxrender-dev libgl1-mesa-dev
# Optional integration/GUI test dependencies:
sudo apt-get install liblilv-dev lilv-utils xvfb xauth
```

Initialize the **pinned** dependencies, including DPF's nested Pugl dependency:

```sh
git submodule update --init --recursive
make
make test
```

The LV2 output is **`build/bin/my_dynamic_eq.lv2/`**, containing `manifest.ttl`, attribution notices, the
complete generated DSP/UI Turtle definitions, and separate DSP and GUI shared
libraries. DPF generates control-port metadata directly from `initParameter()`;
there is no manually maintained, potentially inconsistent second port list.

Equivalent CMake commands:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

`make JOBS=4`, `make BUILD_DIR=build-debug CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Debug`,
and `-DEQ29_BUILD_TESTS=OFF` are supported. CMake selects the host platform's
DPF backend. Linux/X11 is tested here; Windows and macOS builds use the same DSP
and ImGui editor with their native DPF window backends.

## VST2 on Linux, Windows, and macOS

The default build now creates **both LV2 and VST2**. Each operating system needs
its own native binary; a Linux `.so` cannot run as a Windows DLL or macOS bundle.
For a VST2-only build, no LV2 SDK or Lilv is required:

```sh
cmake -S . -B build-vst2 -DCMAKE_BUILD_TYPE=Release -DEQ29_BUILD_LV2=OFF
cmake --build build-vst2 --config Release --parallel 2
ctest --test-dir build-vst2 -C Release --output-on-failure
```

| Platform | Toolchain | VST2 output under `build-vst2/bin/` | Typical user install location |
| --- | --- | --- | --- |
| Linux | GCC/Clang, X11/OpenGL development packages above | `my_dynamic_eq-vst2.so` | `~/.vst/` |
| Windows | Visual Studio 2022 C++ desktop workload and CMake | `my_dynamic_eq-vst2.dll` | A VST2 folder configured in your DAW |
| macOS | Xcode Command Line Tools and CMake | `my_dynamic_eq_vst2.vst` | `~/Library/Audio/Plug-Ins/VST/` |

On Windows run the CMake commands in a Visual Studio developer terminal; add
`-A x64` to the configure command when using the Visual Studio generator.
On macOS install CMake (for example `brew install cmake`) and run `xcode-select
--install` if necessary. For a universal macOS binary, add
`'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` when configuring a **fresh** build folder;
that architecture combination is not validated in the Linux environment.
Use a 64-bit Cocoa-capable VST2 host on macOS. VST2 support must be enabled in
the chosen DAW; hosts that only accept VST3 cannot load this format.

Copy the Linux/Windows binary **and `my_dynamic_eq-licenses/`** from `bin/` to
the host's configured VST2 folder. Copy the whole `.vst` bundle on macOS; its
licenses are already inside. Rescan the plugin folder and select **My Dynamic EQ**.
`cmake --install` stages VST2 into `<prefix>/lib/vst`; point your host there or
copy it to the platform's user plugin directory.

No proprietary VST2 SDK download is needed: DPF includes clean-room VST2 headers,
whose license is shipped alongside the plugin. The native CI workflow builds and
runs DSP, VST2 load, state-reload, and audio tests on Linux, Windows and macOS,
and uploads the platform binaries as workflow artifacts. Native CI results must
be checked separately; only the Linux build and editor are validated locally.

The VST2 version exposes **98 automatable input parameters**. Session state uses
the host's standard normalized parameter-array save/restore, including disabled
band slots. Live meters are sent internally to the custom editor and are not
exposed as writable VST2 parameters. `src/PortMapping.hpp` places VST2 inputs
before outputs as required by DPF while preserving the released LV2 port layout.
Both formats use exactly the same engine, editor, parameter ranges and defaults.

## Install and use

```sh
make install                 # ~/.local/lib/lv2/my_dynamic_eq.lv2
# Or use the traditional LV2 discovery directory:
mkdir -p ~/.lv2
cp -a build/bin/my_dynamic_eq.lv2 ~/.lv2/
```

In Ardour or Carla, rescan LV2 plugins, insert **My Dynamic EQ** on a stereo
track, and open its custom editor. Plugin URI:
`https://github.com/jcantos89/eq29#my_dynamic_eq`.
If the host does not scan `~/.local/lib/lv2`, use `~/.lv2` or configure `LV2_PATH`.
The GUI requires an X11 display (including XWayland) and OpenGL 2-compatible rendering.

- **+ Add** enables a free slot; **Remove** disables the selected slot. Double-click
  an empty place on the graph to add a bell there. All eight slots can be removed.
- Drag a node horizontally for frequency and vertically for static gain. Shift-drag
  vertically or scroll over a node to change Q. The panel offers precise controls.
- **Compression** reduces the band's gain when its detector exceeds Threshold.
  **Upward expansion** increases its gain above Threshold. Range is the maximum
  change in dB, not a replacement for static Gain. Ratio 1:1 leaves gain unchanged.
- **Dynamics enabled** switches just that band's dynamics; static EQ remains active.
- Green is the live response reconstructed from DSP output meters; gray is the
  requested static response. Shading shows the full available dynamic range across
  all enabled bands. The plot is a filter response, not an FFT spectrum analyzer.
- Global Bypass returns the original stereo input, including bypassing Output gain.
  Gain, enable changes and bypass are smoothed. Filter topology changes crossfade.

## DSP and real-time behavior

`src/Biquad.hpp` implements normalized RBJ bell/shelf filters and detector filters.
Each bell detector is a constant-peak bandpass at the band's frequency and Q;
shelf detectors are second-order low/highpasses at the shelf frequency.
All detectors take the original input, so a preceding EQ does not spuriously
trigger another band. The larger absolute detector sample from the two channels
drives a shared peak envelope, avoiding stereo-image movement.

The attack/release controls set one-pole peak-envelope time constants. For an
envelope level `L`, threshold `T`, ratio `R`, and positive maximum range `D`:

```
compression change = -min(D, max(0, L - T) * (1 - 1/R))
expansion change   = +min(D, max(0, L - T) * (R - 1))
EQ gain            = static gain + dynamic change
```

The gain change has an additional 1 ms smoothing stage. Coefficients update every
16 samples, with frequency, Q and static gain smoothed over 10 ms. Topology changes
crossfade for 5 ms. The control schedule persists across host blocks. Parameters
are clamped and nonfinite inputs are sanitized. There is no lookahead or latency,
no allocation or locking in the audio callback, and no internal brickwall limiter.
Extreme boosts can exceed 0 dBFS; leave output headroom.

## Ports and session state

Two audio inputs and two outputs precede the controls. All names, ranges and
defaults live in `src/Parameters.hpp`; each slot has twelve stable input controls:

| Symbol suffix | Range |
| --- | --- |
| `enabled` | 0 / 1 |
| `type` | 0 bell, 1 low shelf, 2 high shelf |
| `frequency` | 20–20000 Hz; DSP clamps to 0.45 × sample rate |
| `gain` | −24 to +24 dB |
| `q` | 0.15–12 |
| `threshold` | −72 to 0 dB |
| `ratio` | 1–20 |
| `attack` | 0.1–200 ms |
| `release` | 5–2000 ms |
| `range` | 0–24 dB |
| `mode` | 0 compression, 1 upward expansion |
| `dynamics` | 0 / 1 |

Symbols are `band1_enabled`, …, `band8_dynamics`. Eight read-only
`bandN_dynamic_gain` meters, global `bypass` and `output_gain`, and a read-only
`sample_rate` port bring the total to 107 control ports (111 including audio).

**Every persistent setting is a standard LV2 input control port.** Hosts save and
restore these values with the session using LV2's normal port-state mechanism.
The active band count is the sum of the eight `enabled` ports, so addition/removal,
filter types and all dynamics settings survive reloads without a private blob or
atom/state worker. Disabled slots also retain their controls; adding through the
GUI deliberately resets the reused slot to defaults. Selection, meter history and
filter delay memories are ephemeral UI/runtime data.

The integration test serializes every input control to Turtle using Lilv state,
reloads it, verifies exact values including a removed and a newly enabled band,
and checks actual output audio from the restored plugin.

## Validation

```sh
make test
# Optional: instantiate the VST2 editor and exercise automation under X11:
xvfb-run -a -s '-screen 0 1280x900x24 +extension GLX' \
  ./build/eq29_vst2_tests "$PWD/build/bin/my_dynamic_eq-vst2.so" --ui
# Instantiate the embedded LV2 GUI and render under a virtual display:
xvfb-run -a -s '-screen 0 1280x900x24 +extension GLX' \
  ./build/eq29_lv2_tests "$PWD/build/bin/my_dynamic_eq.lv2/" --ui
# Host discovery:
LV2_PATH="$PWD/build/bin" lv2ls
```

The core suite checks static response against analytic coefficients, compression,
expansion, off-band rejection, stereo linking, bypass, parameter sanitation,
block partition invariance, reset, and eight-band automation at extreme settings.
The suite runs at 44.1, 48, 96 and 192 kHz. The Lilv test is included when development
headers are found; CMake reports whether it is available. `--ui` needs X11 and
OpenGL; ordinary `make test` needs no display.

The virtual-display test verifies LV2 embedding, rendered pixels, adding/removing
bands, and node dragging through actual X11 mouse events and host parameter writes. An interactive
Ardour/Carla audition and session save/reopen remain recommended before release;
those specific applications are not assumed to have been tested by this harness.

## Files and dependencies

- `src/PluginDSP.cpp`: DPF plugin entry point and LV2 parameter descriptions.
- `src/Engine.hpp`, `src/Biquad.hpp`: fixed-storage stereo DSP and filter response.
- `src/PluginUI.cpp`: ImGui editor, gestures, response graph and parameter bridge.
- `src/PortMapping.hpp`: VST2 parameter-order adapter; LV2 indices stay unchanged.
- `tests/`: DSP regression tests and LV2/VST2 native host/state/GUI integration harnesses.
- `.github/workflows/build.yml`: native Linux, Windows and macOS builds and artifacts.
- `dpf/`, `widgets/`: upstream Git submodules pinned by this repository.
- `scripts/cloud-setup.sh`: optional non-root Debian cloud dependency setup.

See `VALIDATION.md` for the checks actually performed and their limits.

The original project license is CC0-1.0 (`LICENSE`). DPF/DPF-Widgets and their
embedded dependencies retain their own license notices; see their license files.
