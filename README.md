# Sossy X Huttl

Sossy X Huttl is a small original PSP first-person photography game. It uses a
custom 2.5D raycaster rather than Doom code: the camera flash is the weapon,
Kodak film is ammunition, beer restores health, and a contact sheet keeps
the eight best photographs from a run.

The current build is a complete playable prototype for PSP firmware 6.60. The
level art is a procedural brutalist metro treatment: white ceramic tiles,
concrete panels, black tags and a continuous signal-red stripe. Detailed
pixel-art sprites depict Karel in the horse-logo shirt, beer, Kodak film, and a
first-person retro camera. The supplied soundtrack loops from inside the
EBOOT, which also contains the custom horse metro loading screen.

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

On the title screen, Triangle opens the persistent tag-wall rankings. After a
run ends, press Cross on the contact sheet. A qualifying top-20 score opens the
graffiti editor:

- Analog stick: move the tag cursor; D-pad: one-pixel precision
- Hold Cross: draw with the selected black or signal-red ink
- Hold Circle: erase
- Square: switch ink color
- Triangle: undo the previous stroke
- Hold L + R for 0.7 seconds: clear the canvas
- Start: finish, then Cross to save or Circle to keep editing

The tag wall shows five entries per page. Use L/R to change pages, Cross to
start a new run, or Circle to return to the title. Only top-20 runs enter the
editor; tied twentieth-place scores qualify and the newest tied run ranks
first.

Photographs consume one frame even when they miss. One flash can expose
multiple visible enemies. Kodak film adds six frames, up to 24; beer adds 25
vitality, up to 100. Floors are generated from bounded fixed-size storage
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
from Game > Memory Stick when custom firmware/homebrew support is active. The
same EBOOT also supports the PSP-3004.

High scores are saved as `SCORES.DAT` beside the EBOOT. Replacing only the
EBOOT preserves them. The file is versioned, protected by CRC32 and replaced
through temporary/backup files so missing or damaged data cannot prevent the
game from starting.

## Implementation map

- `world.c`: deterministic floor generation, collision, line of sight,
  navigation, enemies, doors and pickups
- `game.c`: run state, difficulty, camera exposure and scoring
- `renderer.c`: 240x136 CPU raycaster, authored RGB565 sprites, first-person
  camera, HUD, flash and results; the PSP GU scales it exactly 2x to 480x272
- `album.c`: fixed-memory RGB565 thumbnails and ranking
- `tag_editor.c`: joystick strokes, black/red ink, erase, clear, undo and
  two-bit tag packing
- `leaderboard.c`: top-20 ranking plus portable, validated score persistence
- `audio.c`: looping PSP MP3 playback plus procedural sound-effect mixer
- `main.c`: PSP callbacks, clock, controls and frame loop
- `tests/test_core.c`: host-side procedural and rules regression suite

The fixed render resolution and fixed entity arrays are deliberate PSP memory
and performance choices. Physical E1004/3004 tests are still the final
authority for screen brightness, analog dead zone, storage and speaker volume.

## Artwork pipeline

- `assets/loading_metro_horse.png`: full-resolution generated master
- `assets/loading_metro_horse_psp.png`: 480x272 PSP/menu version
- `loading_image.c` and `loading_image.h`: embedded 240x136 RGB565 runtime image
- `assets/loading_metro_horse.prompt.txt`: exact generation prompt and mode
- `assets/horse_red.jpg`: exact supplied title artwork
- `title_image.c` and `title_image.h`: embedded 240x136 RGB565 title copy
- `assets/sprites/enemy_karel.png`: 128x128 Karel enemy sprite
- `assets/sprites/beer.png`: 128x128 healing pickup
- `assets/sprites/kodak_film.png`: 128x128 film pickup
- `assets/sprites/camera_first_person.png`: 192x96 held-camera overlay
- `sprite_assets.c` and `sprite_assets.h`: compiled RGB565 sprite data
- `assets/generated/sprite_generation_prompts.txt`: exact built-in prompts
- `assets/soundtrack.mp3`: supplied 48 kHz soundtrack master
- `assets/soundtrack_psp.mp3`: embedded 44.1 kHz PSP-compatible copy

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

To rebuild all detailed sprite PNGs and their embedded RGB565 arrays after
changing one of the generated cutouts:

```sh
python3 tools/prepare_sprite_assets.py
make
```

The PSP MP3 module requires 44.1 kHz audio. Keep the supplied master unchanged
and regenerate its compatible copy with GStreamer when needed:

```sh
sh tools/prepare_soundtrack.sh
make
```

See `assets/ASSET_MANIFEST.md` for the optional final-art handoff. The program
does not need a Doom repository or another engine clone.
