# Doodle Synth

A hand-drawn wavetable synth plugin (VST3) for FL Studio and other DAWs, by TYF.

Draw a waveform, build a 16-frame wavetable, warp it, modulate anything with three point-curve mods,
and run it through a metal phaser, jet flanger, chorus, delay and reverb. Tempo-synced LFOs and mods
follow your DAW's tempo and song position.

## Install (Windows)

1. Open the **Releases** section of this repo and download `DoodleSynth-VST3-Windows.zip` from **latest**.
2. Unzip it. You get a folder called `Doodle Synth.vst3`.
3. Move that folder into `C:\Program Files\Common Files\VST3`.
4. In FL Studio: **Options → Manage plugins → Find installed plugins**, then add **Doodle Synth** from the plugin list.

## How it's built

- `Source/Engine.h` — the sound engine (oscillators, wavetables, filter, envelopes, mods, FX)
- `Source/PluginProcessor.*` — the plugin shell: MIDI, host tempo, saving/loading
- `Source/PluginEditor.*` — shows the faceplate (`ui/index.html`) in a WebView and passes events both ways
- `ui/faceplate.html` — the faceplate page; run `python tools/make_ui.py ui/faceplate.html` after editing it
- Every push to `main` builds the Windows VST3 on GitHub Actions and publishes it as the `latest` release.
