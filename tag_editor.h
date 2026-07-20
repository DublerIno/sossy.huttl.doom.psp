#ifndef SOSSY_X_HUTTL_TAG_EDITOR_H
#define SOSSY_X_HUTTL_TAG_EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#define FF_TAG_WIDTH 48
#define FF_TAG_HEIGHT 20
#define FF_TAG_PIXEL_COUNT (FF_TAG_WIDTH * FF_TAG_HEIGHT)
#define FF_TAG_PACKED_BYTES ((FF_TAG_PIXEL_COUNT + 3) / 4)

typedef enum {
    FF_TAG_EMPTY = 0,
    FF_TAG_BLACK = 1,
    FF_TAG_RED = 2
} FFTagColor;

typedef struct {
    uint8_t data[FF_TAG_PACKED_BYTES];
} FFTagBitmap;

typedef struct {
    float analog_x;
    float analog_y;
    int dpad_x;
    int dpad_y;
    bool draw_held;
    bool erase_held;
    bool color_pressed;
    bool undo_pressed;
    bool clear_held;
    bool finish_pressed;
    bool confirm_pressed;
    bool cancel_pressed;
} FFTagInput;

typedef struct {
    uint8_t pixels[FF_TAG_PIXEL_COUNT];
    uint8_t undo_pixels[FF_TAG_PIXEL_COUNT];
    float cursor_x;
    float cursor_y;
    float clear_hold_seconds;
    float message_seconds;
    FFTagColor ink;
    int stroke_mode;
    bool undo_available;
    bool clear_latched;
    bool confirming;
} FFTagEditor;

void ff_tag_editor_reset(FFTagEditor *editor);
bool ff_tag_editor_update(FFTagEditor *editor, const FFTagInput *input,
                          float delta_seconds);
bool ff_tag_editor_is_blank(const FFTagEditor *editor);
void ff_tag_pack(const uint8_t pixels[FF_TAG_PIXEL_COUNT],
                 FFTagBitmap *bitmap);
void ff_tag_unpack(const FFTagBitmap *bitmap,
                   uint8_t pixels[FF_TAG_PIXEL_COUNT]);
FFTagColor ff_tag_bitmap_pixel(const FFTagBitmap *bitmap, int x, int y);

#endif
