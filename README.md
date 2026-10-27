# Undertow — 3-band subharmonic synthesizer

An original plugin that recreates the *workflow and behaviour* of a dbx 120-style subharmonic
synthesizer with a third "high bass" band (the concept behind Brainworx bx_subsynth).
It generates **new bass one octave below** what you feed it — not an EQ boost.

Formats: **VST3** (Windows + Mac), **AU** (Mac), plus a Standalone app.
Works in **Adobe Audition** and **Ableton Live**.

![UI](docs/undertow_ui.png)

---

## Controls

| Section | Control | What it does |
|---|---|---|
| Input | **Tight Punch** | Resonant high-pass (Off–250 Hz) on the input. Tames boomy existing lows and adds a punch bump at the cutoff before synthesis. |
| Input | **Mono Sub** | Subs are generated from the mid (L+R) signal and placed dead-centre. Leave on unless you want stereo subs. |
| Bands | **LED** | Signal indicator — lights when that band has material to fold down. Boost bands whose LED is lit. |
| Bands | **Trim** | 0 to +18 dB boost into that band's detector. Use for quiet sources so the divider locks on. |
| Bands | **Sub** | Level of that band's generated subharmonic. Meter shows its output. |
| Bands | **Solo** | Hear only that band's sub (dry and Edge muted). Multiple solos combine. |
| Mix | **Subharmonics** | Master level of all generated subs. |
| Mix | **Original** | Dry signal level. Turn fully down (-inf) to get subs only. |
| Mix | **Output** | Final output gain. |
| Edge | **Edge / Smooth–Harsh** | Parallel saturation + compression path. Smooth = tube-ish tanh. Harsh = asymmetric hard clip, trashier. |
| Edge | **Squeeze** | Compression amount (threshold + ratio together). |
| Edge | **Drive** | Saturation depth (auto level-compensated). |
| Edge | **Low / High Cut** | Band-limit what goes into the Edge path. |
| Edge | **Level** | How much Edge is blended in. |

**Band map** (input region → generated sub):

- **Low**: 48–72 Hz → 24–36 Hz
- **Mid**: 72–112 Hz → 36–56 Hz
- **High**: 112–220 Hz → 56–110 Hz (good for snares, toms, guitars, male vocals)

### Quick setup
1. Set **Tight Punch** to just below the lowest note you care about.
2. Watch the LEDs; raise **Sub** on bands that light up. Use **Solo** to audition each.
3. If a band flickers or sounds broken up, raise its **Trim**.
4. Blend with **Subharmonics**. Check on headphones/sub — 24–36 Hz is inaudible on laptop speakers.
5. Optional: switch on **Edge** for grit/density.

---

## How the synthesis works

Each band band-passes its region, watches for positive-going zero crossings (with hysteresis so
noise can't double-trigger) and flips a ±1 "flip-flop" every cycle. Multiplying the band signal by
that flip-flop produces a wave at exactly **half the frequency** that follows the input's
dynamics with no clicks (flips happen at zero). A 6th-order low-pass removes the by-products,
leaving a clean sub. Bands are weighted by which one "owns" a note, and adjacent bands are
polarity-locked so they reinforce instead of cancelling at the crossovers.

Measured (48 kHz, `tests/engine_test.cpp`): octave-down output is flat within about ±2 dB
across 48–220 Hz input, with by-products 35+ dB below the sub.

All DSP lives in `Source/SubEngine.h` (no JUCE dependency) if you want to tweak it.

---

## Building

You need **CMake 3.22+** and a C++ compiler. JUCE is downloaded automatically on first configure.

### Easiest: let GitHub build it
Push this folder to a GitHub repo. The included workflow (`.github/workflows/build.yml`) builds
Windows and Mac versions; download them from the **Actions** tab → latest run → **Artifacts**.

### Windows
1. Install **Visual Studio 2022 Community** with the "Desktop development with C++" workload, and **CMake** (or use the one VS includes).
2. In a "Developer PowerShell for VS 2022" in this folder:
   ```
   cmake -S . -B build
   cmake --build build --config Release
   ```
3. Result: `build\Undertow_artefacts\Release\VST3\Undertow.vst3`

### macOS
1. Install Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).
2. In Terminal in this folder:
   ```
   cmake -S . -B build -G Xcode
   cmake --build build --config Release
   ```
3. Results: `build/Undertow_artefacts/Release/VST3/Undertow.vst3` and `.../AU/Undertow.component`

Tip: set `COPY_PLUGIN_AFTER_BUILD TRUE` in `CMakeLists.txt` to auto-install after each build.

---

## Installing

| | VST3 | AU (Mac only) |
|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` | — |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |

**Mac note:** unsigned plugins downloaded from GitHub may be quarantined. Run once:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Undertow.vst3`
(and the same for the `.component`).

**Ableton Live:** Settings → Plug-Ins → turn on *Use VST3 Plug-In System Folders* (and *Use Audio Units* on Mac) → **Rescan**. Find it under Plug-Ins → Undertow Audio.

**Adobe Audition:** Effects → **Audio Plug-In Manager…** → make sure the VST3 folder is listed → **Scan for Plug-Ins** → enable Undertow. It appears in the Effects menu / Effects Rack.

---

## Running the DSP test
```
cd tests
g++ -O2 -std=c++17 -I../Source engine_test.cpp -o engine_test && ./engine_test
```

## Legal
Undertow is an independent implementation. It contains no code, artwork or assets from Brainworx,
Plugin Alliance or dbx; product names are mentioned only to describe the style of processing.
VST is a trademark of Steinberg. JUCE is used under its own licence (AGPLv3, or JUCE's free Starter / paid licences if you distribute closed-source — personal use is fine; see juce.com/legal).
