# Terrain Oscillator

An [Okllo](https://okllo.com) instrument.

A wavetable synthesizer that plays real places. Pick a region anywhere on Earth,
and its elevation becomes a wavetable: each frame is one line across the land,
and sweeping through the table flies you across it.

Standalone app, VST3 and CLAP (Windows now; macOS/Linux builds planned).

## Status
- Stage 1 (done): C++ engine. Band-limited wavetables (11 mip levels), 16 voices,
  scan modes (manual, LFO sweep, fly per note, fly looping), envelope, filter,
  stereo detune, reverb. Every control is automatable. The table is saved
  with your project. Built-in default table: Mount Fuji (Copernicus GLO-30).
- Stage 2 (done): web interface inside the plugin, with a Find mode (map, region, generate)
  and a Play mode (terrain views, knobs, keys). Includes .wt import, export and Send to Bitwig.
- Stage 3: video recording.
- Stage 4: installers and CI builds for Windows, macOS and Linux.

## Build (Windows)
Requires Visual Studio 2022 Build Tools (C++), which include CMake and Ninja.

    git clone --recurse-submodules https://github.com/inlxs/terrain-oscillator.git
    cd terrain-oscillator
    build.bat

`build.bat` also fetches the submodules and the Microsoft WebView2 SDK (NuGet) on first run.

Artefacts land in `build/TerrainOscillator_artefacts/Release/` (Standalone, VST3, CLAP).
`build/RenderTest_artefacts/Release/RenderTest.exe out.wav` renders a test phrase offline.

## Command-line tools
See `tools/` - `terrain2wt.py` exports wavetables from any GeoTIFF (drone DSM, lidar) or Copernicus 30 m, no app needed.

## Data credits
- Elevation: Terrain Tiles on AWS (Mapzen/Tilezen) - SRTM, GMTED, ETOPO1, EU-DEM and others.
  https://github.com/tilezen/joerd/blob/master/docs/attribution.md
- Optional: Copernicus GLO-30 DEM. Produced using Copernicus WorldDEM-30
  (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018,
  provided under COPERNICUS by the European Union and ESA.
- Map imagery: Sentinel-2 cloudless 2016 by EOX IT Services GmbH (CC BY 4.0), OpenTopoMap (CC-BY-SA).

## Licence
AGPL-3.0-or-later (see LICENSE). Built with JUCE (AGPLv3), clap-juce-extensions (MIT) and Leaflet (BSD-2-Clause, bundled in ui/vendor).
Made by inLXS. Released under the Okllo name (https://okllo.com).
