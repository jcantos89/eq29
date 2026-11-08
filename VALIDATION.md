# Validation — EQ29 1.1

The current instance is Debian 13 x86-64, GCC 14.2, CMake 3.31.6.

## Automated checks

- Default CMake/Make build: LV2 bundle, Linux VST2 shared library and VST3 bundle.
- DSP suite at 44.1, 48, 96 and 192 kHz: existing static/dynamic bell and shelf
  behavior, attack/release, stereo linking, bypass, partition invariance, reset,
  finite output and extreme automation now covering all 16 slots.
- Band 16: external sidechain triggers compression only in its frequency region;
  disconnected external input stays silent and no trigger audio reaches output.
- FFT: a measured 1 kHz tone appears at the right logarithmic display frequency,
  amplitude agrees with dBFS calibration, a +6 dB EQ boost appears in the output
  trace, and antiphase stereo remains visible. Off clears the spectrum meters.
- LV2/Lilv: metadata, 4-input/2-output layout, all 211 input controls restored,
  static/dynamic audio, external sidechain routing, frequency rejection, bypass,
  and actual spectrum outputs. Existing symbols remain stable.
- VST2 native host: all 211 inputs, stable first 98 indices, normalized parameter
  restoration, static/dynamic audio, output gain, bypass and external inputs 3/4.
- VST3 native host: factory/controller, main stereo + auxiliary stereo buses,
  sample processing, band 16, sidechain routing, spectrum output parameter data,
  component-state serialization/restoration verified against output audio.
- Xvfb/OpenGL LV2 and VST2 editor tests: embed/render; add/remove and frequency/gain
  node gestures; external detector selection; host parameter writes. VST2 also
  verifies bypass and balanced automation gestures.
- Xvfb VST3 test: native editor attaches to the host frame, registers a run-loop
  timer and renders nonempty graph/control pixels through that event loop.

- AddressSanitizer + UndefinedBehaviorSanitizer: DSP/FFT regression suite passes.

- Installation staging: LV2 manifest/binaries, standalone VST2 plus licenses,
  and VST3 bundle plus licenses all present; shared-library dependencies resolve.
- Linux archive `eq29-1.1.0-linux-x86_64.tar.gz` contains all three formats.

## Scope

Native GitHub Actions jobs build all three formats on Linux, Windows and macOS
(universal arm64/x86-64 on macOS). Configuration is not a claim that those remote
jobs passed. Windows/macOS native GUI interaction, commercial-DAW audition,
code signing/notarization and restoration of a real DAW project from 1.0 have
not been checked in the Linux machine.

The platform host harnesses verify session parameter mechanisms and VST3 state;
the retained first 98 VST2 indices and original LV2 parameter symbols are explicit
compatibility constraints. A host may require its routing updated for the extra
sidechain inputs, and VST2 sidechain availability depends on host pin routing.

DPF's bundled stb_image implementation emits a GCC warning; it does not prevent
compilation. Our editor does not decode PNG files. Prebuilt Linux files target
the current Debian runtime ABI; build from source for other runtime ABIs.
