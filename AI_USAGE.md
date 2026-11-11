# AI usage

This plugin was built with generative AI.

- **Tool:** Claude Code, Anthropic's coding assistant, using Anthropic's Claude models (Opus).
- **Human role:** Davidian set the requirements and the design direction, supplied the reference schematic, asked for the model to be checked against the published specifications, directed the panel layout and decides what is released.
- **What the AI did:**
  - Read the topology, switch charts and component values off the reference schematic.
  - Looked up the published performance figures for this class of channel and tuned the estimated values to meet them.
  - Wrote the circuit model, the LV2, VST3 and Audio Unit plugin and GUI code and the panel layout. The stroke font, renderer and colour scheme are the Davidian house style, reused from the Bi-Lumen.
  - Wrote the build files for Windows, macOS and Linux, the CI workflow, the LV2 metadata, the tests and the documentation.
- **How it was checked:**
  - Automated tests in `test/`: every sensitivity position against the schematic's switch chart, each EQ band against an independent analog calculation, the high-pass filter against a 3rd-order Butterworth response, the published specifications (frequency response, the 40 kHz point, distortion at +20 dBu from 50 Hz to 10 kHz, maximum output), distortion and saturation behaviour, phase, bypass latency, silence, garbage input, stereo channel independence and CPU cost, plus GUI rendering, GUI interaction checks on Windows and macOS, load checks on Linux and macOS, and the lilv LV2 validator. The VST3 and Audio Unit versions have their own host tests (`vst3_check`, `au_check_mac`), and CI runs Steinberg's VST3 validator and Apple's `auval` on them.
  - `build.bat test` (Windows) and `make test` (macOS, Linux) run them, and GitHub Actions runs them on every platform for each push.
  - No listening test in a DAW has been recorded yet, and no comparison with a recording of the original hardware has been made.
- **Training data and copyright:** no third-party code was knowingly copied. The only reproduced material is the LV2 interface declarations and Steinberg's VST3 C API header, credited in [NOTICE.md](NOTICE.md). The GUI artwork and font are generated from code, not from images.

The AI's output is released under the project licence, GPL-3.0-or-later (see `LICENSE`). Under the GPL, there is no warranty.
