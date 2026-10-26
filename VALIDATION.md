# Validation performed

Validated in the managed Debian 13 x86-64 environment with GCC 14.2 and CMake 3.31.6.

- `make`: builds `build/bin/my_dynamic_eq.lv2`, including separate DSP/UI binaries,
  generated manifest and all 107 control-port definitions. The default build now
  also produces `build/bin/my_dynamic_eq-vst2.so` from the same engine/editor.
- `make test`: the DSP, Lilv integration and native VST2 host suites pass.
- DSP suite: static bell/shelf responses, in-band compression/expansion,
  bandpass and shelf detector rejection, attack/release behavior, stereo linking,
  bypass, block-size invariance, parameter sanitation, reset and extreme eight-band
  automation. Standard workflows exercised at 44.1, 48, 96 and 192 kHz.
- AddressSanitizer + UndefinedBehaviorSanitizer build of the DSP suite: passes.
- Lilv host harness: discovers the bundle and loads the DSP; verifies actual audio
  response, dynamic gain, bypass and sample-rate/meter outputs; serializes all 98
  input controls as Turtle, reloads them, and verifies exact restoration.
- Xvfb/OpenGL GUI harness: embeds the LV2 ImGui editor, renders 80 frames, verifies
  nonempty rendered pixels, then uses real X11 mouse events to add and remove a
  band and drag a frequency/gain node. Host control writes are asserted.
- `lv2ls`: discovers the expected plugin URI.
- `cmake --install build --prefix /tmp/eq29-install`: installs the complete bundle.
- `scripts/cloud-setup.sh`: exercised successfully, including dependency refresh,
  build, tests, and GUI startup. `make` automatically activates the local toolchain.

The build uses pinned DPF and DPF-Widgets submodules. DPF emits a GCC warning in
its bundled stb_image PNG implementation; the build completes without errors.
The GUI uses neither PNG decoding nor an external font file.

No interactive listening test or session reopen was performed in Ardour or Carla.
The included Lilv/X11 harness provides automated LV2 loading, port-state and GUI
integration coverage. Other operating systems and architectures remain untested.
The prebuilt archive targets this Debian 13 x86-64 ABI; build from source on other
Linux distributions when their runtime ABI differs.

## VST2 extension

- Clean VST2-only build (`EQ29_BUILD_LV2=OFF`): DSP and VST2 suites pass;
  VST2 installation staging includes the binary and license directory.
- Native Linux VST2 harness: loads the exported `VSTPluginMain`, checks stereo I/O
  and all 98 automatable inputs, restores normalized session parameters into a
  fresh instance, and measures static EQ, compression, expansion, output gain,
  bypass and stereo output.
- The VST2 parameter adapter is checked in both directions. Meter outputs are
  hidden from VST2 automation without moving any existing LV2 port indices.
- Xvfb VST2 editor test: opens and renders the embedded ImGui editor, verifies its
  pixels, adds/removes a band, drags a frequency/gain node, toggles bypass and
  verifies balanced host automation gestures and DSP parameter values.
- A native GitHub Actions matrix is provided for Linux, Windows and macOS, with
  platform artifacts. Merely adding the workflow does not establish a passing
  Windows/macOS build; those platforms and their editors remain unverified here.
- VST2 hosts must support the format explicitly. No interactive audition in a
  commercial DAW, macOS universal build, code signing or notarization was done.
