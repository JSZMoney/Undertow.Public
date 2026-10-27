# Undertow

A subharmonic synthesizer plugin (AU / VST3) modelled on the dbx 120XP / bx_subsynth, with a
touch of Epicenter. It adds new bass an octave below what you feed it.

![Undertow](docs/undertow_ui.png)

## Install / update (Mac)

Quit your DAW, then paste into Terminal:

```
curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-mac.sh | bash
```

Then rescan plug-ins:

- **Ableton:** Settings → Plug-Ins → Rescan.
- **Audition:** Effects → Audio Plug-In Manager → Scan for Plug-Ins.

To uninstall, delete `Undertow.vst3` and `Undertow.component` from `~/Library/Audio/Plug-Ins/`.

## How to use it

1. Put it on a bass, kick, 808 or full mix.
2. Watch the LED next to each band. Turn up the bands whose LED lights.
3. Solo a band to hear just its sub.
4. Set the overall amount with **Subharmonics**, then blend with **Mix**.
5. Check on headphones or a sub. 24–36 Hz is inaudible on laptop speakers.

## Controls

| | |
|---|---|
| **Subharmonics** | Overall sub level (50% = default, 100% = +12 dB). |
| **24-36 / 36-56 / 56-80 Hz** | Sub level for notes at 48–72 / 72–112 / 112–160 Hz. |
| **Trim** (small knob) | Boosts what a band listens to. Use it if a quiet source doesn't trigger. |
| **Solo** | Hear only that band's sub. This mutes the original and ignores Mix. |
| **Synthesis Input** | *Mid*: subs made from the center and kept mono (recommended). *Stereo*: per side. |
| **Gain In / Gain Out / Mix** | Input level, output level, dry/wet. |
| **Mono Maker / Stereo Width** | Make everything below a frequency mono; widen or narrow the stereo image. |
| **Edge** (power button top right) | Parallel grit: *Smooth* or *Harsh*, Squeeze (compression), Drive, Low/High Cut, Level. |
| **Tight Punch** | Resonant high-pass before synthesis. Tightens boomy lows. |
| **Low End** | ±12 dB bass shelf on the original. |
| **LIMIT** | Safety limiter, on by default. Keeps the output under −0.3 dBFS and shows how much it's pulling down. If that's more than a few dB, lower Gain Out or Subharmonics. Adds 2 ms of latency, which your DAW compensates for automatically. |

The window resizes from its bottom-right corner.

## Building

Every push to `main` builds the Mac and Windows versions on GitHub. The newest build is the
`latest` release, and older builds are kept as `build-N`. To build locally:
`cmake -S . -B build && cmake --build build --config Release` (needs CMake and Xcode tools).

Independent implementation; no code or assets from Brainworx, Plugin Alliance, dbx or AudioControl.
