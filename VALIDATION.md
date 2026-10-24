# Validation performed

Validated in the managed Debian 13 x86-64 environment with GCC 14.2 and CMake 3.31.6.

- `make`: builds `build/bin/my_dynamic_eq.lv2`, including separate DSP/UI binaries,
  generated manifest and all 107 control-port definitions.
- `make test`: both the DSP suite and Lilv integration suite pass.
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
