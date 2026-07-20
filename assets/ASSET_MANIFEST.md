# Sossy X Huttl art handoff

The prototype is self-contained and playable. Map surfaces and sound effects
remain procedural, while the title, loading screen, character, pickups,
first-person camera and soundtrack are authored and compiled into the EBOOT.
The remaining specifications below describe optional future expansion.

## Current loading art

- `horse_red.jpg`: the supplied focal poster
- `inspo/`: material and atmosphere references
- `loading_metro_horse.png`: 1672x941 generated master
- `loading_metro_horse_psp.png`: 480x272 PSP-ready image and EBOOT `PIC1`
- `loading_metro_horse.prompt.txt`: exact built-in generation prompt

The runtime uses a 240x136 RGB565 copy generated into `loading_image.c` and
`loading_image.h`. Run `tools/convert_loading_image.py` as documented in the
README after replacing the master image.

The title screen uses the exact 480x272 `horse_red.jpg` composition, reduced
to the renderer's 240x136 logical resolution by `tools/convert_title_image.py`.

## Visual target

- Minimal brutalist underground station / tagged pedestrian passage
- White ceramic tile, raw gray concrete, soot black and signal red
- Restrained graffiti, pasted paper and cold fluorescent light
- Strong readable silhouettes; avoid detail that vanishes on the PSP screen
- Work at the exact listed pixel size with nearest-neighbor preview at 2x
- Do not use assets extracted from Doom or another copyrighted game

## World textures

Opaque, seamless PNG, 64x64 pixels each:

- `walls/plaster.png`: worn white ceramic metro tile
- `walls/brick.png`: tagged white tile variant
- `walls/panel.png`: raw precast concrete panels and fasteners
- `walls/ritual.png`: torn poster / red horse graphic treatment
- `walls/door.png`: gray steel service or shutter door
- `surfaces/floor.png`: large gray station tiles with dark grout
- `surfaces/ceiling.png`: exposed dark concrete

Keep opposite edges tileable. Texture lighting should be mostly neutral
because the raycaster applies distance and wall-side shading.

## Current sprite art

- `sprites/enemy_karel.png`, 128x128: Karel wearing the white horse shirt
- `sprites/beer.png`, 128x128: healing pickup
- `sprites/kodak_film.png`, 128x128: ammunition pickup
- `sprites/camera_first_person.png`, 192x96: held retro camera and hands
- `generated/*_cutout.png`: high-resolution transparent source cutouts
- `generated/sprite_generation_prompts.txt`: exact built-in prompt set

`tools/prepare_sprite_assets.py` crops, palette-reduces and compiles these into
`sprite_assets.c` and `sprite_assets.h` as PSP RGB565 with a transparent key.
The 128px textures replace the original procedural 64px sprite functions but
retain the 240x136 world buffer for reliable E1004 performance.

## Future sprite animation

Transparent PNG. Keep every frame aligned to the same bottom-center origin.
The single enemy design may be reused for every enemy.

Enemy frames, 128x128 each:

- `enemy/idle_0.png`, `idle_1.png`
- `enemy/walk_0.png` through `walk_3.png`
- `enemy/attack_0.png` through `attack_2.png`
- `enemy/hurt_0.png`
- `enemy/captured_0.png` through `captured_3.png`

Optional world objects:

- `objects/exit.png`, 128x128: archive lift, developing-room door, or portal

Use hard or lightly feathered alpha edges. Leave at least two transparent
pixels around sprites so perspective scaling does not clip them.

## Interface and PSP menu art

- `ui/logo.png`, transparent, maximum 220x48
- `ui/camera_overlay.png`, transparent, 240x136; optional, since the HUD can
  remain code-drawn
- `menu/ICON0.PNG`, 144x80
- `menu/PIC1.PNG`, 480x272, optional PSP menu background

The gameplay viewport is logically 240x136 and displayed at an exact 2x.
Small UI lettering should therefore be designed at 1x and checked at 2x.

## Audio

The current soundtrack master is `soundtrack.mp3` (48 kHz stereo). PSP
firmware's MP3 decoder requires 44.1 kHz, so `soundtrack_psp.mp3` is the
derived 128 kbps stereo copy embedded into the PRX and looped by `audio.c`.
Run `sh tools/prepare_soundtrack.sh` to regenerate it without changing the
master. Procedural sound effects are mixed simultaneously on a separate PSP
audio channel.

Optional authored sound effects should be WAV, signed 16-bit PCM, mono,
44100 Hz (22050 Hz is also acceptable):

- `audio/shutter.wav`
- `audio/empty.wav`
- `audio/exposure_hit.wav`
- `audio/capture.wav`
- `audio/film_pickup.wav`
- `audio/beer_pickup.wav`
- `audio/hurt.wav`
- `audio/door.wav`
- `audio/floor_transition.wav`
- `audio/death.wav`
- `audio/ambience_loop.wav`, a clean-looping 20-45 second room tone

Keep effects short and dry; the PSP speakers lose deep bass and dense stereo
detail. The ambience should leave headroom for the shutter and hit feedback.

## Most useful first delivery

The highest-impact next batch is the seven world textures, enemy animation
frames derived from the current Karel anchor, and the menu icon. Authored sound
effects can follow without blocking level/gameplay work.
