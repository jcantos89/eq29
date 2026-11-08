# EQ29 — My Dynamic EQ 1.1

A stereo dynamic equalizer with **16 band slots**, a live input/output spectrum,
and per-band external sidechain detection. Built with [DPF](https://github.com/DISTRHO/DPF)
and its official [Dear ImGui integration](https://github.com/DISTRHO/DPF-Widgets).
The same DSP and editor build as **LV2, VST2 and VST3 on Linux, Windows and macOS**.

Four bands start enabled. All existing bell/low-shelf/high-shelf filters, dynamic
compression/upward expansion, attack/release/threshold/ratio/range, output gain,
bypass, draggable nodes, Q editing and session persistence remain available.

## Build

Clone with pinned dependencies (including DPF's nested Pugl dependency):

```sh
git clone --recurse-submodules https://github.com/jcantos89/eq29.git
cd eq29
# If already cloned:
git submodule update --init --recursive
```

Requirements: C++17 compiler, CMake 3.20+, OpenGL and the native window toolkit.
On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake pkg-config libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxrender-dev libgl1-mesa-dev
# Optional LV2 integration tests and virtual-display GUI tests:
sudo apt-get install liblilv-dev lilv-utils xvfb xauth
make
make test
```

The managed cloud image activates its local tool prefix automatically through
`make`; `scripts/cloud-setup.sh` can reproduce that non-root Debian setup.

On **Windows**, install Visual Studio 2022's C++ desktop workload and CMake. In a
Visual Studio developer terminal:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

On **macOS**, install Xcode Command Line Tools (`xcode-select --install`) and CMake
(for example `brew install cmake`). For universal Apple Silicon + Intel plugins:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Equivalent native Linux CMake commands omit `CMAKE_OSX_ARCHITECTURES` and `-A x64`.
Each OS needs its own binary; a Linux `.so` cannot run as a Windows DLL or macOS bundle.
Use matching 64-bit hosts. Code signing/notarization is separate from compilation.

All three formats are enabled by default. To build only one, disable the others:

```sh
cmake -S . -B build-vst3 -DCMAKE_BUILD_TYPE=Release -DEQ29_BUILD_LV2=OFF -DEQ29_BUILD_VST2=OFF
cmake --build build-vst3 --config Release --parallel 2
```

Options: `EQ29_BUILD_LV2`, `EQ29_BUILD_VST2`, `EQ29_BUILD_VST3`, `EQ29_BUILD_TESTS`.
At least one format must remain enabled. `make JOBS=4` and `make BUILD_DIR=build-debug
CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Debug` are also supported.

## Binaries and installation

| Format | Linux output in `build/bin/` | Windows | macOS |
| --- | --- | --- | --- |
| LV2 | `my_dynamic_eq.lv2/` | Same bundle name, containing DLLs | Same bundle name, containing dylibs |
| VST2 | `my_dynamic_eq-vst2.so` | `my_dynamic_eq-vst2.dll` | `my_dynamic_eq_vst2.vst/` |
| VST3 | `my_dynamic_eq_vst3.vst3/` | Same bundle name, native Windows contents | Same bundle name, universal native contents when requested |

Copy **whole bundles**, including their licenses. For standalone VST2 `.so`/`.dll`
files, also copy `my_dynamic_eq-licenses/` next to the binary.

| OS | LV2 user folder | VST2 user folder | VST3 user folder |
| --- | --- | --- | --- |
| Linux | `~/.lv2/` | `~/.vst/` | `~/.vst3/` |
| Windows | `%APPDATA%/LV2/` | A VST2 path configured in your host | `%LOCALAPPDATA%/Programs/Common/VST3/` or a host-configured path |
| macOS | `~/Library/Audio/Plug-Ins/LV2/` | `~/Library/Audio/Plug-Ins/VST/` | `~/Library/Audio/Plug-Ins/VST3/` |

Host support and search paths vary, particularly LV2 on Windows/macOS. Rescan your
host and select **My Dynamic EQ**. Linux uses X11/XWayland and OpenGL; DPF uses its
native Windows and Cocoa backends on the other platforms.

`cmake --install build --config Release --prefix stage` stages everything into
`stage/lib/{lv2,vst,vst3}`. `make install` uses `~/.local` on Linux; configure your
host's search paths accordingly or copy the staged bundles to the folders above.

The GitHub Actions workflow builds all three formats on Linux, Windows and macOS
(universal on macOS), tests native loading/audio/state, and uploads each platform's
`build/bin/` as an artifact. Check the actual workflow result before treating a
Windows/macOS build as verified; local validation here is on Linux. DPF supplies
clean-room VST2/VST3 interfaces; no separate proprietary SDK download is needed.

## Editor and live spectrum

- **+ Add** enables a free slot; **Remove** disables the selected slot. Double-click
  the graph to add a bell. Up to 16 bands can be active, and all can be removed.
  The band buttons wrap when space is limited.
- Drag a node horizontally for frequency and vertically for gain. Shift-drag
  vertically or scroll over a node to change Q. The panel provides precise values.
  Hovering the graph shows its frequency and EQ gain.
- **Green** is the live filter response; **gray** is the requested static response.
  Shading shows the available dynamic change. These use the left EQ-gain axis.
- **Blue** is the measured input spectrum; **amber** is the measured output
  spectrum. They share the EQ's logarithmic frequency axis and use the separate
  **right dBFS axis**, so you can see the audio frequencies under each EQ node.
- **Spectrum** selects Off, Input, Output, or Input + Output. This setting is saved
  with the session. Off stops FFT work. The analyzer uses a Hann-windowed 4096-point
  stereo FFT with 50% overlap and 64 logarithmic display bands per trace. Stereo
  antiphase audio stays visible; spectra are not computed from a mono sum.
- Spectrum amplitude is peak dBFS, with a 24 dB/second display fall. FFT frequency
  resolution is sample rate / 4096, so nearby low frequencies can share a display
  peak. This is live measured audio, separate from the analytic filter curve.
- **Dynamics enabled** disables just a band's dynamics. Static EQ remains active.
  Global **Bypass** returns the original stereo input, bypassing Output gain too.

## External sidechain

The plugin has four audio inputs: **main L/R**, followed by **sidechain L/R**;
it always produces just the main stereo output. Select a band and enable
**External SC** to drive that band's detector from the external input. Other
bands can continue to detect the main input independently. The sidechain still
passes through the selected band's detector filter at its Frequency and Q.

1. Insert the EQ on the track you want to process.
2. Route the trigger track to the plugin's sidechain input in your host.
3. Enable **External SC** on the desired band, then set Threshold, Ratio,
   Attack, Release and Range. The live gain meter/green curve show the response.

**VST3** exposes a native stereo auxiliary sidechain bus. **LV2** marks the two
extra ports with `lv2:isSideChain`. **VST2** exposes flat audio inputs 3/4; route
them using the host's four-channel/plugin-pin routing if supported. Some VST2
hosts do not expose external sidechains; VST3 is preferable there.

An unconnected/silent external sidechain causes no above-threshold action; it
never silently falls back to self-detection. Disable External SC for the original
self-detected behavior. Sidechain audio is never mixed into the output.

## DSP and real-time behavior

`Biquad.hpp` implements RBJ bell/shelf EQ and detector filters. Bell detectors are
constant-peak bandpasses at the band's frequency/Q; shelves use low/highpasses.
Each detector reads either original main input or external sidechain, independent
of other EQ bands. The larger absolute detector sample across L/R drives one
linked peak envelope, preserving stereo balance.

For envelope level `L`, threshold `T`, ratio `R`, and maximum range `D` in dB:

```
compression change = -min(D, max(0, L - T) * (1 - 1/R))
expansion change   = +min(D, max(0, L - T) * (R - 1))
EQ gain            = static gain + dynamic change
```

Ratio 1:1 leaves the gain unchanged. Attack/release control envelope time
constants; the gain has an additional 1 ms smoothing stage. Coefficients update
every 16 samples, with 10 ms frequency/Q/static-gain smoothing and a 5 ms topology
crossfade. Bypass is smoothed. There is no lookahead/latency or brickwall limiter;
leave headroom for boosts.

Audio processing, sidechain detection and FFT use fixed storage with no locks,
allocations, file/network I/O or UI access. Spectrum/meter data travels through
DPF's host parameter bridge, including separate-process LV2 editors; no unsafe
DSP/UI shared pointer is needed.

## Parameters and session compatibility

Each band retains its 12 controls: Enabled, Type (bell/low/high shelf), Frequency
(20–20000 Hz), Gain (±24 dB), Q (0.15–12), Threshold (−72–0 dB), Ratio (1–20),
Attack (0.1–200 ms), Release (5–2000 ms), Range (0–24 dB), Mode (compression/upward
expansion) and Dynamics enabled. Frequency clamps to 0.45 × sample rate.
Each band also has an External SC switch. Output gain ranges from −24 to +12 dB.

There are **211 persistent input controls** and **145 output controls** (16 dynamic
gain meters, sample rate and 128 spectrum values): 356 controls plus six audio
ports in LV2. Spectrum data and filter histories are transient and not saved.
The active band count is the sum of the 16 Enabled controls.

LV2 symbols for existing bands/globals remain unchanged; additional bands,
sidechain switches and spectrum controls are appended in the parameter namespace.
The two added audio inputs move physical control-port indices: LV2 hosts restore
by **symbol**, as usual. The first 98 VST2 normalized input parameter indices
remain identical to version 1.0, including Bypass/Output gain. Added band slots
start disabled and sidechain switches start off. Hosts can restore old sessions
without new bands changing their sound; a host may need its pin routing reviewed
after discovering the additional inputs.

LV2 hosts save input port values; VST2 hosts save normalized input parameter
arrays; VST3 serializes input parameters by symbol through DPF's component state.
All per-band detector selections and the spectrum-view mode are included.

## Tests

```sh
make test
# Linux GUI startup and interactions:
xvfb-run -a ./build/eq29_lv2_tests "$PWD/build/bin/my_dynamic_eq.lv2/" --ui
xvfb-run -a ./build/eq29_vst2_tests "$PWD/build/bin/my_dynamic_eq-vst2.so" --ui
xvfb-run -a ./build/eq29_vst3_tests "$PWD/build/bin/my_dynamic_eq_vst3.vst3/Contents/x86_64-linux/my_dynamic_eq_vst3.so" --ui
```

The last command uses the x86-64 Linux bundle path. CTest uses the native target
path automatically on every platform. DSP, VST2 and VST3 tests require no display;
the optional Lilv/LV2 test is built on Linux when development headers are present.
See `VALIDATION.md` for actual checks and platform limitations.

Source: `Engine.hpp` (DSP), `Spectrum.hpp` (FFT), `Parameters.hpp`/`PortMapping.hpp`
(stable host controls), `PluginDSP.cpp` (DPF entry points), `PluginUI.cpp` (ImGui),
`tests/` (DSP and native format hosts), and `.github/workflows/build.yml` (platform CI).

The project is CC0-1.0 (`LICENSE`). DPF, ImGui, fonts and embedded interfaces keep
their own licenses, included in each binary distribution.
