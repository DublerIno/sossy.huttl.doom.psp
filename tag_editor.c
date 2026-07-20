#include "tag_editor.h"

#include <math.h>
#include <string.h>

#define FF_TAG_CURSOR_SPEED 24.0f
#define FF_TAG_CLEAR_HOLD_SECONDS 0.7f

static int ff_tag_clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float ff_tag_clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int ff_tag_cursor_x(const FFTagEditor *editor)
{
    return ff_tag_clamp_int((int)lroundf(editor->cursor_x),
                            0, FF_TAG_WIDTH - 1);
}

static int ff_tag_cursor_y(const FFTagEditor *editor)
{
    return ff_tag_clamp_int((int)lroundf(editor->cursor_y),
                            0, FF_TAG_HEIGHT - 1);
}

static void ff_tag_snapshot(FFTagEditor *editor)
{
    memcpy(editor->undo_pixels, editor->pixels, sizeof(editor->pixels));
    editor->undo_available = true;
}

static void ff_tag_set_pixel(FFTagEditor *editor, int x, int y,
                             FFTagColor color)
{
    if ((unsigned int)x >= FF_TAG_WIDTH ||
        (unsigned int)y >= FF_TAG_HEIGHT) return;
    editor->pixels[y * FF_TAG_WIDTH + x] = (uint8_t)color;
}

static void ff_tag_line(FFTagEditor *editor, int x0, int y0,
                        int x1, int y1, FFTagColor color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ff_tag_set_pixel(editor, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int twice = error * 2;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }
}

void ff_tag_editor_reset(FFTagEditor *editor)
{
    memset(editor, 0, sizeof(*editor));
    editor->cursor_x = (float)(FF_TAG_WIDTH - 1) * 0.5f;
    editor->cursor_y = (float)(FF_TAG_HEIGHT - 1) * 0.5f;
    editor->ink = FF_TAG_BLACK;
}

bool ff_tag_editor_is_blank(const FFTagEditor *editor)
{
    int pixel;
    for (pixel = 0; pixel < FF_TAG_PIXEL_COUNT; ++pixel) {
        if (editor->pixels[pixel] != FF_TAG_EMPTY) return false;
    }
    return true;
}

bool ff_tag_editor_update(FFTagEditor *editor, const FFTagInput *input,
                          float delta_seconds)
{
    int old_x;
    int old_y;
    int new_x;
    int new_y;
    int requested_mode;
    FFTagColor requested_color;

    if (delta_seconds < 0.0f) delta_seconds = 0.0f;
    if (delta_seconds > 0.1f) delta_seconds = 0.1f;
    if (editor->message_seconds > 0.0f) {
        editor->message_seconds -= delta_seconds;
        if (editor->message_seconds < 0.0f) editor->message_seconds = 0.0f;
    }

    if (editor->confirming) {
        if (input->confirm_pressed) return true;
        if (input->cancel_pressed) editor->confirming = false;
        return false;
    }

    if (input->finish_pressed) {
        editor->stroke_mode = 0;
        if (ff_tag_editor_is_blank(editor)) {
            editor->message_seconds = 1.5f;
        } else {
            editor->confirming = true;
        }
        return false;
    }

    if (input->color_pressed) {
        editor->ink = editor->ink == FF_TAG_BLACK ? FF_TAG_RED : FF_TAG_BLACK;
        editor->stroke_mode = 0;
    }
    if (input->undo_pressed && editor->undo_available) {
        memcpy(editor->pixels, editor->undo_pixels, sizeof(editor->pixels));
        editor->undo_available = false;
        editor->stroke_mode = 0;
        return false;
    }

    if (input->clear_held) {
        editor->clear_hold_seconds += delta_seconds;
        if (!editor->clear_latched &&
            editor->clear_hold_seconds >= FF_TAG_CLEAR_HOLD_SECONDS) {
            if (!ff_tag_editor_is_blank(editor)) {
                ff_tag_snapshot(editor);
                memset(editor->pixels, FF_TAG_EMPTY, sizeof(editor->pixels));
            }
            editor->clear_latched = true;
            editor->stroke_mode = 0;
            return false;
        }
    } else {
        editor->clear_hold_seconds = 0.0f;
        editor->clear_latched = false;
    }

    old_x = ff_tag_cursor_x(editor);
    old_y = ff_tag_cursor_y(editor);
    editor->cursor_x += input->analog_x * FF_TAG_CURSOR_SPEED * delta_seconds;
    editor->cursor_y += input->analog_y * FF_TAG_CURSOR_SPEED * delta_seconds;
    editor->cursor_x += (float)input->dpad_x;
    editor->cursor_y += (float)input->dpad_y;
    editor->cursor_x = ff_tag_clamp_float(editor->cursor_x, 0.0f,
                                         (float)(FF_TAG_WIDTH - 1));
    editor->cursor_y = ff_tag_clamp_float(editor->cursor_y, 0.0f,
                                         (float)(FF_TAG_HEIGHT - 1));
    new_x = ff_tag_cursor_x(editor);
    new_y = ff_tag_cursor_y(editor);

    requested_mode = input->erase_held ? 2 : (input->draw_held ? 1 : 0);
    requested_color = requested_mode == 2 ? FF_TAG_EMPTY : editor->ink;
    if (requested_mode == 0) {
        editor->stroke_mode = 0;
        return false;
    }
    if (editor->stroke_mode != requested_mode) {
        ff_tag_snapshot(editor);
        editor->stroke_mode = requested_mode;
    }
    ff_tag_line(editor, old_x, old_y, new_x, new_y, requested_color);
    return false;
}

void ff_tag_pack(const uint8_t pixels[FF_TAG_PIXEL_COUNT],
                 FFTagBitmap *bitmap)
{
    int pixel;
    memset(bitmap->data, 0, sizeof(bitmap->data));
    for (pixel = 0; pixel < FF_TAG_PIXEL_COUNT; ++pixel) {
        unsigned int value = pixels[pixel] <= FF_TAG_RED
            ? pixels[pixel] : FF_TAG_EMPTY;
        bitmap->data[pixel >> 2] |= (uint8_t)(value << ((pixel & 3) * 2));
    }
}

void ff_tag_unpack(const FFTagBitmap *bitmap,
                   uint8_t pixels[FF_TAG_PIXEL_COUNT])
{
    int pixel;
    for (pixel = 0; pixel < FF_TAG_PIXEL_COUNT; ++pixel) {
        pixels[pixel] = (uint8_t)((bitmap->data[pixel >> 2] >>
                                  ((pixel & 3) * 2)) & 3u);
    }
}

FFTagColor ff_tag_bitmap_pixel(const FFTagBitmap *bitmap, int x, int y)
{
    int pixel;
    unsigned int value;
    if ((unsigned int)x >= FF_TAG_WIDTH ||
        (unsigned int)y >= FF_TAG_HEIGHT) return FF_TAG_EMPTY;
    pixel = y * FF_TAG_WIDTH + x;
    value = (bitmap->data[pixel >> 2] >> ((pixel & 3) * 2)) & 3u;
    return value <= FF_TAG_RED ? (FFTagColor)value : FF_TAG_EMPTY;
}
