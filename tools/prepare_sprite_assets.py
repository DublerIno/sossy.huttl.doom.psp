#!/usr/bin/env python3
"""Prepare generated RGBA art and compile it into PSP RGB565 sprite arrays."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
TRANSPARENT_5650 = 0x07E0


@dataclass(frozen=True)
class SpriteSpec:
    name: str
    input_path: Path
    output_path: Path
    width: int
    height: int
    content_width: int
    content_height: int
    bottom_align: bool = False


SPECS = (
    SpriteSpec(
        "enemy", ROOT / "assets/generated/enemy_karel_cutout.png",
        ROOT / "assets/sprites/enemy_karel.png", 128, 128, 104, 124, True,
    ),
    SpriteSpec(
        "beer", ROOT / "assets/generated/beer_cutout.png",
        ROOT / "assets/sprites/beer.png", 128, 128, 66, 118, True,
    ),
    SpriteSpec(
        "film", ROOT / "assets/generated/kodak_film_cutout.png",
        ROOT / "assets/sprites/kodak_film.png", 128, 128, 122, 104, False,
    ),
    SpriteSpec(
        "camera", ROOT / "assets/generated/camera_cutout.png",
        ROOT / "assets/sprites/camera_first_person.png", 192, 96, 190, 94,
        True,
    ),
)


def psp_5650(red: int, green: int, blue: int) -> int:
    return ((blue >> 3) << 11) | ((green >> 2) << 5) | (red >> 3)


def crop_visible(image: Image.Image) -> Image.Image:
    alpha = image.getchannel("A")
    bbox = alpha.point(lambda value: 255 if value >= 16 else 0).getbbox()
    if bbox is None:
        raise ValueError("sprite contains no visible pixels")
    left, top, right, bottom = bbox
    padding_x = max(2, (right - left) // 100)
    padding_y = max(2, (bottom - top) // 100)
    return image.crop((
        max(0, left - padding_x), max(0, top - padding_y),
        min(image.width, right + padding_x),
        min(image.height, bottom + padding_y),
    ))


def reduce_palette(image: Image.Image, colors: int = 96) -> Image.Image:
    alpha = image.getchannel("A")
    rgb = image.convert("RGB").quantize(
        colors=colors, method=Image.Quantize.FASTOCTREE,
        dither=Image.Dither.NONE,
    ).convert("RGB")
    hard_alpha = alpha.point(lambda value: 255 if value >= 96 else 0)
    rgb.putalpha(hard_alpha)
    return rgb


def prepare(spec: SpriteSpec) -> Image.Image:
    with Image.open(spec.input_path) as opened:
        cropped = crop_visible(opened.convert("RGBA"))
    scale = min(
        spec.content_width / cropped.width,
        spec.content_height / cropped.height,
    )
    resized = cropped.resize(
        (max(1, round(cropped.width * scale)),
         max(1, round(cropped.height * scale))),
        Image.Resampling.LANCZOS,
    )
    resized = reduce_palette(resized)
    canvas = Image.new("RGBA", (spec.width, spec.height), (0, 0, 0, 0))
    x = (spec.width - resized.width) // 2
    y = spec.height - resized.height - 1 if spec.bottom_align else \
        (spec.height - resized.height) // 2
    canvas.alpha_composite(resized, (x, y))
    spec.output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(spec.output_path, format="PNG", optimize=True)
    return canvas


def macro_name(spec: SpriteSpec) -> str:
    return f"FF_{spec.name.upper()}_TEXTURE"


def symbol_name(spec: SpriteSpec) -> str:
    return f"g_ff_{spec.name}_texture"


def write_header(path: Path) -> None:
    lines = [
        "#ifndef SOSSY_X_HUTTL_SPRITE_ASSETS_H",
        "#define SOSSY_X_HUTTL_SPRITE_ASSETS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define FF_SPRITE_TRANSPARENT 0x{TRANSPARENT_5650:04x}u",
    ]
    for spec in SPECS:
        macro = macro_name(spec)
        lines.extend((
            f"#define {macro}_WIDTH {spec.width}",
            f"#define {macro}_HEIGHT {spec.height}",
        ))
    lines.append("")
    for spec in SPECS:
        macro = macro_name(spec)
        lines.append(
            f"extern const uint16_t {symbol_name(spec)}["
            f"{macro}_WIDTH * {macro}_HEIGHT];"
        )
    lines.extend(("", "#endif", ""))
    path.write_text("\n".join(lines), encoding="ascii")


def write_source(path: Path, images: dict[str, Image.Image]) -> None:
    with path.open("w", encoding="ascii") as output:
        output.write('#include "sprite_assets.h"\n\n')
        for spec in SPECS:
            macro = macro_name(spec)
            output.write(
                f"const uint16_t {symbol_name(spec)}["
                f"{macro}_WIDTH * {macro}_HEIGHT] "
                "__attribute__((aligned(64))) = {\n"
            )
            values: list[int] = []
            for red, green, blue, alpha in images[spec.name].getdata():
                values.append(
                    TRANSPARENT_5650 if alpha < 128
                    else psp_5650(red, green, blue)
                )
            for start in range(0, len(values), 12):
                row = ", ".join(
                    f"0x{value:04x}u" for value in values[start:start + 12]
                )
                output.write(f"    {row},\n")
            output.write("};\n\n")


def main() -> None:
    images = {spec.name: prepare(spec) for spec in SPECS}
    write_header(ROOT / "sprite_assets.h")
    write_source(ROOT / "sprite_assets.c", images)
    for spec in SPECS:
        print(f"{spec.output_path.relative_to(ROOT)}: {spec.width}x{spec.height}")


if __name__ == "__main__":
    main()
