# Sossy X Huttl

Sossy X Huttl is a small original PSP first-person photography game. It uses a
custom 2.5D raycaster rather than Doom code: the camera flash is the weapon,
film is ammunition, darkroom tonic restores health, and a contact sheet keeps
the eight best photographs from a run.

The current build is a complete playable prototype for PSP firmware 6.60. The
level art is a procedural brutalist metro treatment: white ceramic tiles,
concrete panels, black tags and a continuous signal-red stripe. Sound and most
visuals are generated in code; the custom horse metro loading screen is
compiled into the EBOOT and is also used as its PSP menu background.

## Play

- Analog stick: move forward/back and turn
- D-pad: digital movement and turning fallback
- L / R: strafe left / right
- Cross: take a photograph; centered, close and unobstructed subjects receive
  the strongest exposure
- Circle: open the door or use the floor exit you are facing
- Start: pause
- Triangle: toggle the performance/minimap overlay
- Select: quit to the PSP menu

Photographs consume one frame even when they miss. One flash can expose
multiple visible enemies. Film canisters add six frames, up to 24; tonic adds
25 vitality, up to 100. Floors are generated from bounded fixed-size storage
and continue indefinitely while enemy count, health and speed rise to caps.

## Build

Install a current PSPDEV toolchain and put its `bin` directory on `PATH`, then:

```sh
make test
make
```

The output is `EBOOT.PBP`. If PSPDEV is installed outside the standard path,
set its environment first:

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
make
```

To install on a PSP, make a directory such as
`ms0:/PSP/GAME/SOSSYXH/` and copy `EBOOT.PBP` into it. The E1004 can launch it
from Game > Memory Stick when custom firmware/homebrew support is active.

## Implementation map

- `world.c`: deterministic floor generation, collision, line of sight,
  navigation, enemies, doors and pickups
- `game.c`: run state, difficulty, camera exposure and scoring
- `renderer.c`: 240x136 CPU raycaster, procedural art, HUD, flash and results;
  the PSP GU scales it exactly 2x to 480x272
- `album.c`: fixed-memory RGB565 thumbnails and ranking
- `audio.c`: procedural ambience and sound-effect mixer
- `main.c`: PSP callbacks, clock, controls and frame loop
- `tests/test_core.c`: host-side procedural and rules regression suite

The fixed render resolution and fixed entity arrays are deliberate PSP memory
and performance choices. A physical E1004 test is still the final authority
for screen brightness, analog dead zone and speaker volume.

## Artwork pipeline

- `assets/loading_metro_horse.png`: full-resolution generated master
- `assets/loading_metro_horse_psp.png`: 480x272 PSP/menu version
- `loading_image.c` and `loading_image.h`: embedded 240x136 RGB565 runtime image
- `assets/loading_metro_horse.prompt.txt`: exact generation prompt and mode
- `assets/horse_red.jpg`: exact supplied title artwork
- `title_image.c` and `title_image.h`: embedded 240x136 RGB565 title copy

To replace the loading art, provide a PNG or high-quality JPG. A 16:9 image at
960x544 or larger is ideal; keep important details away from the bottom-right
status box. With Pillow installed, regenerate the PSP and compiled versions:

```sh
python3 tools/convert_loading_image.py \
  assets/your_loading_image.png \
  assets/loading_metro_horse_psp.png \
  loading_image.c loading_image.h
make
```

To rebuild the title image from the supplied 480x272 artwork:

```sh
python3 tools/convert_title_image.py \
  assets/horse_red.jpg title_image.c title_image.h
make
```

See `assets/ASSET_MANIFEST.md` for the optional final-art handoff. The program
does not need a Doom repository or another engine clone.
