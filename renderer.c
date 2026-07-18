#include "renderer.h"

#include "loading_image.h"
#include "title_image.h"

#include <math.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FF_PI 3.14159265358979323846f
#define FF_PLANE_LENGTH 0.5773502692f
#define FF_DISPLAY_WIDTH 480
#define FF_DISPLAY_HEIGHT 272
#define FF_DISPLAY_STRIDE 512
#define FF_DISPLAY_BUFFER_BYTES (FF_DISPLAY_STRIDE * FF_DISPLAY_HEIGHT * 2)
#define FF_SPRITE_CAPACITY (FF_MAX_ENEMIES + FF_MAX_PICKUPS + 1)

typedef struct {
    float u;
    float v;
    float x;
    float y;
    float z;
} FFTextureVertex;

typedef enum {
    FF_SPRITE_ENEMY = 0,
    FF_SPRITE_FILM,
    FF_SPRITE_TONIC,
    FF_SPRITE_EXIT
} FFSpriteType;

typedef struct {
    float x;
    float y;
    float distance_squared;
    FFSpriteType type;
    int index;
} FFRenderSprite;

typedef struct {
    char character;
    uint8_t columns[5];
} FFGlyph;

static unsigned int g_display_list[65536] __attribute__((aligned(16)));
static uint16_t g_pixels[FF_RENDER_STRIDE * 256] __attribute__((aligned(64)));
static float g_depth[FF_RENDER_WIDTH];
static unsigned int g_visual_frame;

static const FFGlyph FF_FONT[] = {
    {'A', {0x7e, 0x11, 0x11, 0x11, 0x7e}},
    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7f, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3e, 0x41, 0x49, 0x49, 0x7a}},
    {'H', {0x7f, 0x08, 0x08, 0x08, 0x7f}},
    {'I', {0x00, 0x41, 0x7f, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3f, 0x01}},
    {'K', {0x7f, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'N', {0x7f, 0x04, 0x08, 0x10, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7f, 0x01, 0x01}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'W', {0x3f, 0x40, 0x38, 0x40, 0x3f}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}},
    {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}},
    {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}},
    {'5', {0x2f, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3e, 0x49, 0x49, 0x49, 0x32}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x26, 0x49, 0x49, 0x49, 0x3e}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'+', {0x08, 0x08, 0x3e, 0x08, 0x08}},
    {'!', {0x00, 0x00, 0x5f, 0x00, 0x00}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}}
};

static uint16_t ff_rgb(unsigned int red, unsigned int green, unsigned int blue)
{
    return (uint16_t)(((blue & 0xf8u) << 8) |
                      ((green & 0xfcu) << 3) | (red >> 3));
}

static uint16_t ff_shade(uint16_t color, float amount)
{
    unsigned int red = color & 31u;
    unsigned int green = (color >> 5) & 63u;
    unsigned int blue = (color >> 11) & 31u;
    if (amount < 0.0f) {
        amount = 0.0f;
    }
    red = (unsigned int)((float)red * amount);
    green = (unsigned int)((float)green * amount);
    blue = (unsigned int)((float)blue * amount);
    if (red > 31u) red = 31u;
    if (green > 63u) green = 63u;
    if (blue > 31u) blue = 31u;
    return (uint16_t)((blue << 11) | (green << 5) | red);
}

static void ff_pixel(int x, int y, uint16_t color)
{
    if ((unsigned int)x < FF_RENDER_WIDTH &&
        (unsigned int)y < FF_RENDER_HEIGHT) {
        g_pixels[y * FF_RENDER_STRIDE + x] = color;
    }
}

static void ff_fill(uint16_t color)
{
    int y;
    for (y = 0; y < FF_RENDER_HEIGHT; ++y) {
        int x;
        for (x = 0; x < FF_RENDER_WIDTH; ++x) {
            g_pixels[y * FF_RENDER_STRIDE + x] = color;
        }
    }
}

static void ff_rect(int x, int y, int width, int height, uint16_t color)
{
    int row;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > FF_RENDER_WIDTH) width = FF_RENDER_WIDTH - x;
    if (y + height > FF_RENDER_HEIGHT) height = FF_RENDER_HEIGHT - y;
    if (width <= 0 || height <= 0) return;
    for (row = 0; row < height; ++row) {
        int column;
        uint16_t *destination = &g_pixels[(y + row) * FF_RENDER_STRIDE + x];
        for (column = 0; column < width; ++column) {
            destination[column] = color;
        }
    }
}

static void ff_frame(int x, int y, int width, int height, uint16_t color)
{
    ff_rect(x, y, width, 1, color);
    ff_rect(x, y + height - 1, width, 1, color);
    ff_rect(x, y, 1, height, color);
    ff_rect(x + width - 1, y, 1, height, color);
}

static void ff_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ff_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int twice = error * 2;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }
}

static const uint8_t *ff_glyph(char character)
{
    unsigned int i;
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    for (i = 0; i < sizeof(FF_FONT) / sizeof(FF_FONT[0]); ++i) {
        if (FF_FONT[i].character == character) return FF_FONT[i].columns;
    }
    return NULL;
}

static void ff_text(int x, int y, const char *text, int scale, uint16_t color)
{
    while (*text != '\0') {
        const uint8_t *glyph = ff_glyph(*text++);
        if (glyph != NULL) {
            int column;
            for (column = 0; column < 5; ++column) {
                int row;
                for (row = 0; row < 7; ++row) {
                    if (glyph[column] & (1u << row)) {
                        ff_rect(x + column * scale, y + row * scale,
                                scale, scale, color);
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static int ff_text_width(const char *text, int scale)
{
    return (int)strlen(text) * 6 * scale;
}

static void ff_text_centered(int y, const char *text, int scale, uint16_t color)
{
    ff_text((FF_RENDER_WIDTH - ff_text_width(text, scale)) / 2,
            y, text, scale, color);
}

static uint32_t ff_hash(unsigned int x, unsigned int y, unsigned int seed)
{
    uint32_t value = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ seed;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

static uint16_t ff_floor_color(int map_x, int map_y, int tex_x, int tex_y)
{
    uint32_t noise = ff_hash((unsigned int)(map_x * 32 + tex_x),
                             (unsigned int)(map_y * 32 + tex_y), 0x39a17u);
    int grain = (int)(noise & 7u) - 3;
    bool grout = tex_x % 8 == 0 || tex_y % 8 == 0;
    if (grout) {
        return ff_rgb(43, 44, 43);
    }
    if ((noise & 127u) == 0u) {
        return ff_rgb(56, 54, 52);
    }
    return ff_rgb((unsigned int)(82 + grain),
                  (unsigned int)(83 + grain),
                  (unsigned int)(81 + grain));
}

static uint16_t ff_ceiling_color(int map_x, int map_y, int tex_x, int tex_y)
{
    uint32_t noise = ff_hash((unsigned int)(map_x * 32 + tex_x),
                             (unsigned int)(map_y * 32 + tex_y), 0xbadf00du);
    int grain = (int)(noise & 7u) - 3;
    int stain = ((noise >> 7) & 63u) == 0u ? -15 : 0;
    bool seam = tex_x == 0 || tex_y == 0;
    if (seam) return ff_rgb(31, 32, 32);
    return ff_rgb((unsigned int)(57 + grain + stain),
                  (unsigned int)(58 + grain + stain),
                  (unsigned int)(58 + grain + stain));
}

static int ff_int_abs(int value)
{
    return value < 0 ? -value : value;
}

static uint16_t ff_wall_color(uint8_t tile, int tex_x, int tex_y,
                              int map_x, int map_y, int side)
{
    uint16_t color;
    uint32_t noise = ff_hash((unsigned int)(map_x * 64 + tex_x),
                             (unsigned int)(map_y * 64 + tex_y),
                             (unsigned int)tile * 0x2719u);
    int grain = (int)(noise & 15u) - 7;
    bool grout = tex_x % 8 == 0 || tex_y % 8 == 0;
    bool signal_stripe = tex_y >= 39 && tex_y <= 45;

    switch (tile) {
    case FF_TILE_WALL_BRICK: {
        int stroke_one = 13 + ((tex_x * 3 + map_x * 5) % 18);
        int stroke_two = 32 - ((tex_x * 2 + map_y * 3) % 16);
        bool tag = tex_y > 10 && tex_y < 34 &&
                   (ff_int_abs(tex_y - stroke_one) <= 1 ||
                    ff_int_abs(tex_y - stroke_two) <= 1 ||
                    (((tex_x + map_x * 3) % 19) < 2 && tex_y > 14));
        if (tag) color = ff_rgb(24, 25, 25);
        else if (grout) color = ff_rgb(73, 75, 74);
        else color = ff_rgb((unsigned int)(171 + grain),
                            (unsigned int)(172 + grain),
                            (unsigned int)(168 + grain));
        break;
    }
    case FF_TILE_WALL_PANEL: {
        bool seam = tex_x < 2 || tex_x > 61 || tex_y < 2 || tex_y > 61 ||
                    tex_x == 31;
        bool bolt = ((tex_x == 5 || tex_x == 58) &&
                     (tex_y == 6 || tex_y == 57));
        if (bolt) color = ff_rgb(30, 31, 31);
        else if (seam) color = ff_rgb(43, 45, 45);
        else color = ff_rgb((unsigned int)(105 + grain),
                            (unsigned int)(107 + grain),
                            (unsigned int)(106 + grain));
        break;
    }
    case FF_TILE_WALL_RITUAL: {
        bool poster = tex_x >= 7 && tex_x <= 56 && tex_y >= 6 && tex_y <= 36;
        if (poster) {
            int dx = tex_x - 32;
            int dy = tex_y - 21;
            int ellipse = dx * dx * 2 + dy * dy * 8;
            bool print = (ellipse > 190 && ellipse < 430) ||
                         (tex_y > 17 && tex_y < 25 &&
                          ((tex_x * 5 + tex_y * 3) % 17 < 6));
            bool edge = tex_x == 7 || tex_x == 56 || tex_y == 6 || tex_y == 36;
            if (print) color = ff_rgb(163, 20, 25);
            else if (edge) color = ff_rgb(111, 104, 94);
            else color = ff_rgb(205, 200, 185);
        } else if (grout) {
            color = ff_rgb(71, 73, 72);
        } else {
            color = ff_rgb((unsigned int)(169 + grain),
                           (unsigned int)(171 + grain),
                           (unsigned int)(168 + grain));
        }
        break;
    }
    case FF_TILE_DOOR: {
        bool edge = tex_x < 5 || tex_x > 58 || tex_y < 4;
        bool groove = tex_x % 8 < 2;
        bool handle = tex_x > 47 && tex_x < 53 && tex_y > 31 && tex_y < 37;
        if (handle) color = ff_rgb(36, 36, 35);
        else if (edge) color = ff_rgb(37, 39, 40);
        else if (groove) color = ff_rgb(79, 82, 83);
        else color = ff_rgb((unsigned int)(111 + grain),
                            (unsigned int)(114 + grain),
                            (unsigned int)(115 + grain));
        break;
    }
    case FF_TILE_WALL_PLASTER:
    default: {
        bool crack = ((tex_x + map_x * 7) % 29 == 0 && tex_y > 18) ||
                     ((tex_y + map_y * 11) % 37 == 0 && tex_x > 30);
        if (crack) color = ff_rgb(63, 65, 64);
        else if (grout) color = ff_rgb(75, 77, 76);
        else color = ff_rgb((unsigned int)(177 + grain),
                            (unsigned int)(178 + grain),
                            (unsigned int)(174 + grain));
        break;
    }
    }
    if (signal_stripe) {
        color = (noise & 63u) == 0u ? ff_rgb(87, 75, 72)
                                    : ff_rgb(190, 19, 27);
    }
    return side ? ff_shade(color, 0.76f) : color;
}

static void ff_render_floor_and_ceiling(float direction_x, float direction_y,
                                        float plane_x, float plane_y,
                                        float player_x, float player_y)
{
    int horizon = FF_RENDER_HEIGHT / 2;
    float left_x = direction_x - plane_x;
    float left_y = direction_y - plane_y;
    float right_x = direction_x + plane_x;
    float right_y = direction_y + plane_y;
    int y;

    ff_rect(0, 0, FF_RENDER_WIDTH, horizon, ff_rgb(70, 69, 64));
    ff_rect(0, horizon, FF_RENDER_WIDTH,
            FF_RENDER_HEIGHT - horizon, ff_rgb(48, 45, 40));

    for (y = horizon + 1; y < FF_RENDER_HEIGHT; ++y) {
        float row_distance = (0.5f * (float)FF_RENDER_HEIGHT) /
                             (float)(y - horizon);
        float step_x = row_distance * (right_x - left_x) /
                       (float)FF_RENDER_WIDTH;
        float step_y = row_distance * (right_y - left_y) /
                       (float)FF_RENDER_WIDTH;
        float floor_x = player_x + row_distance * left_x;
        float floor_y = player_y + row_distance * left_y;
        int ceiling_y = FF_RENDER_HEIGHT - y - 1;
        int x;
        for (x = 0; x < FF_RENDER_WIDTH; ++x) {
            int cell_x = (int)floorf(floor_x);
            int cell_y = (int)floorf(floor_y);
            int tex_x = ((int)(32.0f * (floor_x - floorf(floor_x)))) & 31;
            int tex_y = ((int)(32.0f * (floor_y - floorf(floor_y)))) & 31;
            float darkness = 1.0f / (1.0f + row_distance * 0.055f);
            g_pixels[y * FF_RENDER_STRIDE + x] = ff_shade(
                ff_floor_color(cell_x, cell_y, tex_x, tex_y), darkness);
            if (ceiling_y >= 0) {
                g_pixels[ceiling_y * FF_RENDER_STRIDE + x] = ff_shade(
                    ff_ceiling_color(cell_x, cell_y, tex_x, tex_y), darkness);
            }
            floor_x += step_x;
            floor_y += step_y;
        }
    }
}

static void ff_render_walls(const FFGame *game,
                            float direction_x, float direction_y,
                            float plane_x, float plane_y)
{
    int screen_x;
    for (screen_x = 0; screen_x < FF_RENDER_WIDTH; ++screen_x) {
        float camera_x = 2.0f * (float)screen_x / (float)FF_RENDER_WIDTH - 1.0f;
        float ray_x = direction_x + plane_x * camera_x;
        float ray_y = direction_y + plane_y * camera_x;
        int map_x = (int)floorf(game->player_x);
        int map_y = (int)floorf(game->player_y);
        float delta_x = ray_x == 0.0f ? 1.0e20f : fabsf(1.0f / ray_x);
        float delta_y = ray_y == 0.0f ? 1.0e20f : fabsf(1.0f / ray_y);
        float side_x;
        float side_y;
        int step_x;
        int step_y;
        int side = 0;
        int guard = 0;
        uint8_t tile = FF_TILE_WALL_PLASTER;
        float distance;
        float wall_x;
        int tex_x;
        int line_height;
        int draw_start;
        int draw_end;
        int y;

        if (ray_x < 0.0f) {
            step_x = -1;
            side_x = (game->player_x - (float)map_x) * delta_x;
        } else {
            step_x = 1;
            side_x = ((float)map_x + 1.0f - game->player_x) * delta_x;
        }
        if (ray_y < 0.0f) {
            step_y = -1;
            side_y = (game->player_y - (float)map_y) * delta_y;
        } else {
            step_y = 1;
            side_y = ((float)map_y + 1.0f - game->player_y) * delta_y;
        }

        while (guard++ < FF_MAP_SIZE * 2) {
            if (side_x < side_y) {
                side_x += delta_x;
                map_x += step_x;
                side = 0;
            } else {
                side_y += delta_y;
                map_y += step_y;
                side = 1;
            }
            if (!ff_world_in_bounds(map_x, map_y)) break;
            tile = game->world.tiles[map_y * FF_MAP_SIZE + map_x];
            if (ff_world_is_solid(&game->world, map_x, map_y)) break;
        }

        distance = side == 0 ? side_x - delta_x : side_y - delta_y;
        if (distance < 0.01f) distance = 0.01f;
        g_depth[screen_x] = distance;
        line_height = (int)((float)FF_RENDER_HEIGHT / distance);
        draw_start = -line_height / 2 + FF_RENDER_HEIGHT / 2;
        draw_end = line_height / 2 + FF_RENDER_HEIGHT / 2;
        if (draw_start < 0) draw_start = 0;
        if (draw_end >= FF_RENDER_HEIGHT) draw_end = FF_RENDER_HEIGHT - 1;

        wall_x = side == 0
            ? game->player_y + distance * ray_y
            : game->player_x + distance * ray_x;
        wall_x -= floorf(wall_x);
        tex_x = (int)(wall_x * 64.0f) & 63;
        if ((side == 0 && ray_x > 0.0f) || (side == 1 && ray_y < 0.0f)) {
            tex_x = 63 - tex_x;
        }
        for (y = draw_start; y <= draw_end; ++y) {
            int tex_y = (((y * 2 - FF_RENDER_HEIGHT + line_height) * 64) /
                         (line_height * 2)) & 63;
            float fog = 1.0f / (1.0f + distance * 0.070f);
            g_pixels[y * FF_RENDER_STRIDE + screen_x] = ff_shade(
                ff_wall_color(tile, tex_x, tex_y, map_x, map_y, side), fog);
        }
    }
}

static bool ff_enemy_texel(const FFEnemy *enemy, int x, int y, uint16_t *color)
{
    float u = ((float)x + 0.5f) / 64.0f;
    float v = ((float)y + 0.5f) / 64.0f;
    float dx = u - 0.5f;
    float head_y = v - 0.235f;
    bool head = dx * dx / 0.0256f + head_y * head_y / 0.020f < 1.0f;
    bool horn_left = v > 0.035f && v < 0.23f &&
                     fabsf(u - (0.32f - (v - 0.035f) * 0.55f)) < 0.025f;
    bool horn_right = v > 0.035f && v < 0.23f &&
                      fabsf(u - (0.68f + (v - 0.035f) * 0.55f)) < 0.025f;
    bool robe = v >= 0.31f && v < 0.96f &&
                fabsf(dx) < 0.18f + (v - 0.31f) * 0.25f;
    bool arms = v > 0.39f && v < 0.68f &&
                fabsf(dx) < 0.37f - fabsf(v - 0.54f) * 0.25f;
    bool eye;

    if (!(head || horn_left || horn_right || robe || arms)) return false;
    if (enemy->state == FF_ENEMY_CAPTURED &&
        ((x + y + (int)g_visual_frame) & 3) != 0) return false;

    eye = head && v > 0.21f && v < 0.25f && fabsf(dx) > 0.035f &&
          fabsf(dx) < 0.105f;
    if (eye) {
        *color = enemy->state == FF_ENEMY_HURT
            ? ff_rgb(255, 240, 220) : ff_rgb(205, 40, 33);
    } else if (head) {
        *color = ff_rgb(171, 165, 147);
    } else if (horn_left || horn_right) {
        *color = ff_rgb(42, 37, 32);
    } else if (enemy->state == FF_ENEMY_HURT || enemy->state == FF_ENEMY_CAPTURED) {
        *color = ff_rgb(203, 197, 178);
    } else {
        int fold = ((x / 6) & 1) ? 8 : 0;
        *color = ff_rgb((unsigned int)(28 + fold),
                        (unsigned int)(27 + fold),
                        (unsigned int)(25 + fold));
    }
    return true;
}

static bool ff_pickup_texel(FFSpriteType type, int x, int y, uint16_t *color)
{
    float u = ((float)x + 0.5f) / 64.0f;
    float v = ((float)y + 0.5f) / 64.0f;
    float dx = u - 0.5f;
    if (type == FF_SPRITE_FILM) {
        bool body = v > 0.26f && v < 0.80f && fabsf(dx) < 0.27f;
        bool cap = v > 0.20f && v <= 0.30f && fabsf(dx) < 0.20f;
        bool roll = dx * dx + (v - 0.52f) * (v - 0.52f) < 0.075f;
        if (!(body || cap || roll)) return false;
        if (roll && dx * dx + (v - 0.52f) * (v - 0.52f) < 0.018f) {
            *color = ff_rgb(24, 23, 22);
        } else if ((x + y) % 11 < 2) {
            *color = ff_rgb(206, 191, 125);
        } else {
            *color = ff_rgb(137, 119, 68);
        }
        return true;
    }
    {
        bool bottle = (v > 0.28f && v < 0.84f && fabsf(dx) < 0.24f) ||
                      (v > 0.15f && v <= 0.32f && fabsf(dx) < 0.11f);
        bool cork = v > 0.10f && v <= 0.18f && fabsf(dx) < 0.09f;
        if (!(bottle || cork)) return false;
        if (cork) *color = ff_rgb(120, 87, 55);
        else if (v > 0.51f && fabsf(dx) < 0.20f) {
            *color = ((x + y) & 3) ? ff_rgb(85, 24, 27) : ff_rgb(142, 37, 38);
        } else *color = ff_rgb(155, 168, 156);
        return true;
    }
}

static bool ff_exit_texel(int x, int y, uint16_t *color)
{
    float u = ((float)x + 0.5f) / 64.0f;
    float v = ((float)y + 0.5f) / 64.0f;
    float dx = u - 0.5f;
    float dy = v - 0.50f;
    float oval = dx * dx / 0.105f + dy * dy / 0.225f;
    bool ring = oval > 0.72f && oval < 1.08f;
    bool inside = oval <= 0.72f;
    if (!ring && !inside) return false;
    if (ring) {
        int pulse = (int)((g_visual_frame / 4u) & 7u);
        *color = ff_rgb((unsigned int)(142 + pulse * 6),
                        (unsigned int)(129 + pulse * 4), 72);
    } else if (((x * 3 + y + (int)g_visual_frame) & 15) < 2) {
        *color = ff_rgb(158, 149, 104);
    } else {
        *color = ff_rgb(18, 24, 24);
    }
    return true;
}

static bool ff_sprite_texel(const FFGame *game, const FFRenderSprite *sprite,
                            int x, int y, uint16_t *color)
{
    if (sprite->type == FF_SPRITE_ENEMY) {
        return ff_enemy_texel(&game->world.enemies[sprite->index], x, y, color);
    }
    if (sprite->type == FF_SPRITE_EXIT) return ff_exit_texel(x, y, color);
    return ff_pickup_texel(sprite->type, x, y, color);
}

static void ff_render_sprites(const FFGame *game,
                              float direction_x, float direction_y,
                              float plane_x, float plane_y)
{
    FFRenderSprite sprites[FF_SPRITE_CAPACITY];
    int count = 0;
    int i;
    float determinant = plane_x * direction_y - direction_x * plane_y;
    float inverse = fabsf(determinant) < 0.00001f ? 0.0f : 1.0f / determinant;

    for (i = 0; i < game->world.enemy_count && count < FF_SPRITE_CAPACITY; ++i) {
        const FFEnemy *enemy = &game->world.enemies[i];
        if (enemy->active) {
            float dx = enemy->x - game->player_x;
            float dy = enemy->y - game->player_y;
            sprites[count].x = enemy->x;
            sprites[count].y = enemy->y;
            sprites[count].distance_squared = dx * dx + dy * dy;
            sprites[count].type = FF_SPRITE_ENEMY;
            sprites[count].index = i;
            ++count;
        }
    }
    for (i = 0; i < game->world.pickup_count && count < FF_SPRITE_CAPACITY; ++i) {
        const FFPickup *pickup = &game->world.pickups[i];
        if (pickup->active) {
            float dx = pickup->x - game->player_x;
            float dy = pickup->y - game->player_y;
            sprites[count].x = pickup->x;
            sprites[count].y = pickup->y;
            sprites[count].distance_squared = dx * dx + dy * dy;
            sprites[count].type = pickup->type == FF_PICKUP_FILM
                ? FF_SPRITE_FILM : FF_SPRITE_TONIC;
            sprites[count].index = i;
            ++count;
        }
    }
    if (count < FF_SPRITE_CAPACITY) {
        float dx = game->world.exit_x - game->player_x;
        float dy = game->world.exit_y - game->player_y;
        sprites[count].x = game->world.exit_x;
        sprites[count].y = game->world.exit_y;
        sprites[count].distance_squared = dx * dx + dy * dy;
        sprites[count].type = FF_SPRITE_EXIT;
        sprites[count].index = 0;
        ++count;
    }

    for (i = 1; i < count; ++i) {
        FFRenderSprite item = sprites[i];
        int at = i - 1;
        while (at >= 0 && sprites[at].distance_squared < item.distance_squared) {
            sprites[at + 1] = sprites[at];
            --at;
        }
        sprites[at + 1] = item;
    }

    for (i = 0; i < count; ++i) {
        const FFRenderSprite *sprite = &sprites[i];
        float relative_x = sprite->x - game->player_x;
        float relative_y = sprite->y - game->player_y;
        float transform_x = inverse * (direction_y * relative_x -
                                       direction_x * relative_y);
        float transform_y = inverse * (-plane_y * relative_x +
                                       plane_x * relative_y);
        float scale = sprite->type == FF_SPRITE_ENEMY ? 1.05f :
                      (sprite->type == FF_SPRITE_EXIT ? 1.18f : 0.52f);
        int sprite_height;
        int sprite_width;
        int screen_center;
        int start_y;
        int end_y;
        int start_x;
        int end_x;
        int x;

        if (transform_y <= 0.12f) continue;
        sprite_height = (int)(fabsf((float)FF_RENDER_HEIGHT / transform_y) * scale);
        if (sprite_height < 2) continue;
        sprite_width = sprite_height;
        screen_center = (int)((float)FF_RENDER_WIDTH * 0.5f *
                              (1.0f + transform_x / transform_y));
        start_y = FF_RENDER_HEIGHT / 2 - sprite_height / 2;
        if (sprite->type == FF_SPRITE_FILM || sprite->type == FF_SPRITE_TONIC) {
            start_y += sprite_height / 2;
        }
        end_y = start_y + sprite_height;
        start_x = screen_center - sprite_width / 2;
        end_x = start_x + sprite_width;
        if (end_y < 0 || start_y >= FF_RENDER_HEIGHT ||
            end_x < 0 || start_x >= FF_RENDER_WIDTH) continue;

        for (x = start_x < 0 ? 0 : start_x;
             x < end_x && x < FF_RENDER_WIDTH; ++x) {
            int texture_x;
            int y;
            if (transform_y >= g_depth[x]) continue;
            texture_x = (x - start_x) * 64 / sprite_width;
            for (y = start_y < 0 ? 0 : start_y;
                 y < end_y && y < FF_RENDER_HEIGHT; ++y) {
                int texture_y = (y - start_y) * 64 / sprite_height;
                uint16_t color;
                if (ff_sprite_texel(game, sprite, texture_x, texture_y, &color)) {
                    float fog = 1.0f / (1.0f + transform_y * 0.055f);
                    g_pixels[y * FF_RENDER_STRIDE + x] = ff_shade(color, fog);
                }
            }
        }
    }
}

void ff_renderer_render_world(const FFGame *game)
{
    float direction_x = cosf(game->player_angle);
    float direction_y = sinf(game->player_angle);
    float plane_x = -direction_y * FF_PLANE_LENGTH;
    float plane_y = direction_x * FF_PLANE_LENGTH;
    ff_render_floor_and_ceiling(direction_x, direction_y, plane_x, plane_y,
                                game->player_x, game->player_y);
    ff_render_walls(game, direction_x, direction_y, plane_x, plane_y);
    ff_render_sprites(game, direction_x, direction_y, plane_x, plane_y);
    ++g_visual_frame;
}

void ff_renderer_render_loading(int floor_number, float progress)
{
    uint16_t pale = ff_rgb(231, 229, 219);
    uint16_t red = ff_rgb(196, 20, 27);
    char text[32];
    int y;

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    for (y = 0; y < FF_RENDER_HEIGHT; ++y) {
        memcpy(&g_pixels[y * FF_RENDER_STRIDE],
               &g_ff_loading_image[y * FF_LOADING_IMAGE_WIDTH],
               FF_LOADING_IMAGE_WIDTH * sizeof(uint16_t));
    }

    ff_rect(155, 111, 80, 21, ff_rgb(13, 14, 14));
    ff_frame(155, 111, 80, 21, pale);
    if (floor_number > 0) {
        snprintf(text, sizeof(text), "FLOOR %02d", floor_number);
    } else {
        snprintf(text, sizeof(text), "INITIALIZING");
    }
    ff_text(160, 115, text, 1, pale);
    ff_frame(160, 124, 70, 5, pale);
    ff_rect(161, 125, (int)(68.0f * progress), 3, red);
    ++g_visual_frame;
}

static void ff_apply_flash(float amount)
{
    int y;
    if (amount <= 0.0f) return;
    if (amount > 1.0f) amount = 1.0f;
    for (y = 0; y < FF_RENDER_HEIGHT; ++y) {
        int x;
        for (x = 0; x < FF_RENDER_WIDTH; ++x) {
            uint16_t color = g_pixels[y * FF_RENDER_STRIDE + x];
            unsigned int red = color & 31u;
            unsigned int green = (color >> 5) & 63u;
            unsigned int blue = (color >> 11) & 31u;
            red += (unsigned int)((31u - red) * amount);
            green += (unsigned int)((63u - green) * amount);
            blue += (unsigned int)((31u - blue) * amount);
            g_pixels[y * FF_RENDER_STRIDE + x] =
                (uint16_t)((blue << 11) | (green << 5) | red);
        }
    }
}

static void ff_draw_minimap(const FFGame *game)
{
    int origin_x = FF_RENDER_WIDTH - FF_MAP_SIZE - 5;
    int origin_y = 4;
    int y;
    ff_rect(origin_x - 2, origin_y - 2, FF_MAP_SIZE + 4, FF_MAP_SIZE + 4,
            ff_rgb(11, 12, 12));
    for (y = 0; y < FF_MAP_SIZE; ++y) {
        int x;
        for (x = 0; x < FF_MAP_SIZE; ++x) {
            uint8_t tile = game->world.tiles[y * FF_MAP_SIZE + x];
            uint16_t color = ff_rgb(26, 27, 27);
            if (tile == FF_TILE_FLOOR) color = ff_rgb(72, 71, 65);
            else if (tile == FF_TILE_DOOR) color = ff_rgb(134, 91, 50);
            else if (tile == FF_TILE_EXIT) color = ff_rgb(190, 170, 83);
            ff_pixel(origin_x + x, origin_y + y, color);
        }
    }
    for (y = 0; y < game->world.enemy_count; ++y) {
        const FFEnemy *enemy = &game->world.enemies[y];
        if (enemy->active) {
            ff_pixel(origin_x + (int)enemy->x, origin_y + (int)enemy->y,
                     ff_rgb(187, 42, 38));
        }
    }
    ff_rect(origin_x + (int)game->player_x - 1,
            origin_y + (int)game->player_y - 1, 3, 3, ff_rgb(237, 232, 207));
}

void ff_renderer_draw_hud(const FFGame *game, bool show_debug,
                          float frames_per_second, float frame_milliseconds)
{
    uint16_t pale = ff_rgb(226, 223, 204);
    uint16_t red = ff_rgb(189, 39, 37);
    char text[64];
    int health_width;
    int film_width;

    ff_apply_flash(game->flash_amount);

    /* Thin viewfinder corners leave the scene readable. */
    ff_line(42, 18, 60, 18, pale); ff_line(42, 18, 42, 31, pale);
    ff_line(197, 18, 179, 18, pale); ff_line(197, 18, 197, 31, pale);
    ff_line(42, 101, 60, 101, pale); ff_line(42, 101, 42, 88, pale);
    ff_line(197, 101, 179, 101, pale); ff_line(197, 101, 197, 88, pale);
    ff_frame(116, 56, 9, 9, pale);
    ff_pixel(120, 60, red);

    /* Camera body and status readout. */
    ff_rect(0, 112, FF_RENDER_WIDTH, 24, ff_rgb(15, 16, 16));
    ff_rect(0, 112, FF_RENDER_WIDTH, 1, ff_rgb(108, 105, 95));
    ff_text(5, 116, "VITAL", 1, pale);
    ff_frame(35, 116, 52, 7, ff_rgb(103, 101, 91));
    health_width = game->health * 50 / FF_MAX_HEALTH;
    ff_rect(36, 117, health_width, 5,
            game->health <= 25 ? red : ff_rgb(188, 197, 166));

    ff_text(5, 126, "FILM", 1, pale);
    ff_frame(35, 126, 52, 7, ff_rgb(103, 101, 91));
    film_width = game->film * 50 / FF_MAX_FILM;
    ff_rect(36, 127, film_width, 5,
            game->film == 0 ? red : ff_rgb(194, 167, 89));

    snprintf(text, sizeof(text), "FLOOR %d", game->floor_number);
    ff_text(96, 116, text, 1, pale);
    snprintf(text, sizeof(text), "SCORE %d", game->score);
    ff_text(96, 126, text, 1, pale);
    snprintf(text, sizeof(text), "%02d/%02d", game->film, FF_MAX_FILM);
    ff_text(197, 116, text, 1, pale);
    snprintf(text, sizeof(text), "X %d", game->total_captures);
    ff_text(203, 126, text, 1, pale);

    if (show_debug) {
        ff_draw_minimap(game);
        ff_rect(3, 3, 102, 20, ff_rgb(10, 11, 11));
        snprintf(text, sizeof(text), "FPS %2.1F  %2.1FMS",
                 (double)frames_per_second, (double)frame_milliseconds);
        ff_text(6, 6, text, 1, ff_rgb(170, 217, 184));
        snprintf(text, sizeof(text), "E %d  X %.1F Y %.1F",
                 ff_world_active_enemy_count(&game->world),
                 (double)game->player_x, (double)game->player_y);
        ff_text(6, 14, text, 1, ff_rgb(170, 217, 184));
    }

    if (game->mode == FF_MODE_PAUSED) {
        int y;
        for (y = 24; y < 105; ++y) {
            int x;
            for (x = 34; x < 206; ++x) {
                if (((x + y) & 1) == 0) ff_pixel(x, y, ff_rgb(16, 17, 17));
            }
        }
        ff_frame(49, 39, 142, 49, pale);
        ff_text_centered(49, "PAUSED", 2, pale);
        ff_text_centered(70, "START TO RETURN", 1, pale);
    }
}

void ff_renderer_render_title(void)
{
    uint16_t paper = ff_rgb(252, 249, 244);
    uint16_t ink = ff_rgb(17, 16, 15);
    uint16_t red = ff_rgb(166, 14, 17);
    int y;

    for (y = 0; y < FF_RENDER_HEIGHT; ++y) {
        memcpy(&g_pixels[y * FF_RENDER_STRIDE],
               &g_ff_title_image[y * FF_TITLE_IMAGE_WIDTH],
               FF_TITLE_IMAGE_WIDTH * sizeof(uint16_t));
    }

    ff_rect(70, 117, 100, 14, paper);
    ff_frame(70, 117, 100, 14, red);
    ff_text_centered(121, "PRESS X TO PLAY", 1, ink);
    ++g_visual_frame;
}

static void ff_draw_thumbnail(const FFStoredPhoto *photo,
                              int destination_x, int destination_y,
                              int width, int height)
{
    int y;
    for (y = 0; y < height; ++y) {
        int source_y = y * FF_THUMB_HEIGHT / height;
        int x;
        for (x = 0; x < width; ++x) {
            int source_x = x * FF_THUMB_WIDTH / width;
            ff_pixel(destination_x + x, destination_y + y,
                     photo->pixels[source_y * FF_THUMB_WIDTH + source_x]);
        }
    }
}

void ff_renderer_render_results(const FFGame *game, const FFAlbum *album)
{
    uint16_t paper = ff_rgb(220, 216, 198);
    uint16_t ink = ff_rgb(22, 23, 22);
    char text[64];
    int rank;
    ff_fill(ff_rgb(36, 36, 33));
    ff_rect(3, 3, 234, 130, paper);
    ff_text(8, 7, "CASE CONTACT SHEET", 1, ink);
    snprintf(text, sizeof(text), "SCORE %d  FLOOR %d  CAPTURES %d",
             game->score, game->floor_number, game->total_captures);
    ff_text(8, 16, text, 1, ink);

    if (album->count == 0) {
        ff_text_centered(60, "NO EXPOSED FRAMES", 1, ink);
    } else {
        for (rank = 0; rank < album->count; ++rank) {
            const FFStoredPhoto *photo = ff_album_ranked(album, rank);
            int column = rank & 3;
            int row = rank >> 2;
            int x = 7 + column * 58;
            int y = 28 + row * 42;
            ff_draw_thumbnail(photo, x, y, 54, 31);
            ff_frame(x - 1, y - 1, 56, 33, ink);
            snprintf(text, sizeof(text), "%d:%d", rank + 1, photo->score);
            ff_text(x, y + 34, text, 1, ink);
        }
    }
    ff_text(8, 119, "X NEW CASE", 1, ink);
    ff_text(152, 119, "SELECT QUIT", 1, ink);
    ++g_visual_frame;
}

void ff_renderer_initialize(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, g_display_list);
    sceGuDrawBuffer(GU_PSM_5650, (void *)0, FF_DISPLAY_STRIDE);
    sceGuDispBuffer(FF_DISPLAY_WIDTH, FF_DISPLAY_HEIGHT,
                    (void *)(uintptr_t)FF_DISPLAY_BUFFER_BYTES,
                    FF_DISPLAY_STRIDE);
    sceGuOffset(2048 - FF_DISPLAY_WIDTH / 2, 2048 - FF_DISPLAY_HEIGHT / 2);
    sceGuViewport(2048, 2048, FF_DISPLAY_WIDTH, FF_DISPLAY_HEIGHT);
    sceGuScissor(0, 0, FF_DISPLAY_WIDTH, FF_DISPLAY_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void ff_renderer_present(void)
{
    FFTextureVertex *vertices;
    sceKernelDcacheWritebackRange(g_pixels, sizeof(g_pixels));
    sceGuStart(GU_DIRECT, g_display_list);
    sceGuClearColor(0xff000000u);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
    sceGuTexImage(0, 256, 256, FF_RENDER_STRIDE, g_pixels);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    vertices = (FFTextureVertex *)sceGuGetMemory(2 * sizeof(*vertices));
    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[0].x = 0.0f;
    vertices[0].y = 0.0f;
    vertices[0].z = 0.0f;
    vertices[1].u = (float)FF_RENDER_WIDTH;
    vertices[1].v = (float)FF_RENDER_HEIGHT;
    vertices[1].x = (float)FF_DISPLAY_WIDTH;
    vertices[1].y = (float)FF_DISPLAY_HEIGHT;
    vertices[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   2, NULL, vertices);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

void ff_renderer_shutdown(void)
{
    sceGuTerm();
}

const uint16_t *ff_renderer_pixels(void)
{
    return g_pixels;
}

int ff_renderer_stride(void)
{
    return FF_RENDER_STRIDE;
}
