#!/usr/bin/env python3
"""Compile the supplied 480x272 title artwork as a PSP RGB565 texture."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


DISPLAY_SIZE = (480, 272)
LOGICAL_SIZE = (240, 136)


def psp_5650(red: int, green: int, blue: int) -> int:
    """Pack PSP GU_PSM_5650: red in the low bits, blue in the high bits."""
    return ((blue >> 3) << 11) | ((green >> 2) << 5) | (red >> 3)


def write_header(path: Path) -> None:
    path.write_text(
        "#ifndef SOSSY_X_HUTTL_TITLE_IMAGE_H\n"
        "#define SOSSY_X_HUTTL_TITLE_IMAGE_H\n\n"
        "#include <stdint.h>\n\n"
        "#define FF_TITLE_IMAGE_WIDTH 240\n"
        "#define FF_TITLE_IMAGE_HEIGHT 136\n\n"
        "extern const uint16_t "
        "g_ff_title_image[FF_TITLE_IMAGE_WIDTH * FF_TITLE_IMAGE_HEIGHT];\n\n"
        "#endif\n",
        encoding="ascii",
    )


def write_source(path: Path, image: Image.Image) -> None:
    pixels = [psp_5650(*pixel) for pixel in image.getdata()]
    with path.open("w", encoding="ascii") as output:
        output.write('#include "title_image.h"\n\n')
        output.write(
            "const uint16_t g_ff_title_image[FF_TITLE_IMAGE_WIDTH * "
            "FF_TITLE_IMAGE_HEIGHT] __attribute__((aligned(64))) = {\n"
        )
        for start in range(0, len(pixels), 12):
            values = ", ".join(
                f"0x{value:04x}u" for value in pixels[start:start + 12]
            )
            output.write(f"    {values},\n")
        output.write("};\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("header", type=Path)
    arguments = parser.parse_args()

    with Image.open(arguments.input) as opened:
        if opened.size != DISPLAY_SIZE:
            raise SystemExit(
                f"title image must be exactly 480x272, got "
                f"{opened.width}x{opened.height}"
            )
        full_screen = opened.convert("RGB")

    # The renderer is 240x136 and the GU presents it at an exact 2x scale.
    logical = full_screen.resize(LOGICAL_SIZE, Image.Resampling.BOX)
    write_header(arguments.header)
    write_source(arguments.source, logical)


if __name__ == "__main__":
    main()
