#ifndef FLASHFRAME_RENDERER_H
#define FLASHFRAME_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "album.h"
#include "game.h"
#include "leaderboard.h"
#include "tag_editor.h"

#define FF_RENDER_WIDTH 240
#define FF_RENDER_HEIGHT 136
#define FF_RENDER_STRIDE 256

void ff_renderer_initialize(void);
void ff_renderer_shutdown(void);
void ff_renderer_render_world(const FFGame *game);
void ff_renderer_render_loading(int floor_number, float progress);
void ff_renderer_draw_hud(const FFGame *game, bool show_debug,
                          float frames_per_second, float frame_milliseconds);
void ff_renderer_render_title(void);
void ff_renderer_render_results(const FFGame *game, const FFAlbum *album);
void ff_renderer_render_tag_editor(const FFTagEditor *editor, int score);
void ff_renderer_render_leaderboard(const FFLeaderboard *leaderboard,
                                    int page, int highlight_rank,
                                    const char *notice);
void ff_renderer_present(void);

const uint16_t *ff_renderer_pixels(void);
int ff_renderer_stride(void);

#endif
