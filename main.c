#include "album.h"
#include "audio.h"
#include "game.h"
#include "leaderboard.h"
#include "renderer.h"
#include "tag_editor.h"

#include <math.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdint.h>
#include <string.h>

PSP_MODULE_INFO("Sossy X Huttl", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(8192);

#define FF_LOADING_FRAMES 100

static volatile int g_running = 1;
static FFGame g_game;
static FFAlbum g_album;
static FFLeaderboard g_leaderboard;
static FFTagEditor g_tag_editor;
static char g_score_path[FF_LEADERBOARD_PATH_CAPACITY];
static const char *g_load_notice;
static const char *g_leaderboard_notice;
static int g_leaderboard_page;
static int g_highlight_rank = -1;
static bool g_tag_wait_for_release;

static int ff_exit_callback(int argument_one, int argument_two, void *common)
{
    (void)argument_one;
    (void)argument_two;
    (void)common;
    g_running = 0;
    return 0;
}

static int ff_callback_thread(SceSize argument_size, void *argument)
{
    SceUID callback;
    (void)argument_size;
    (void)argument;
    callback = sceKernelCreateCallback("Sossy X Huttl exit", ff_exit_callback,
                                       NULL);
    if (callback >= 0) {
        sceKernelRegisterExitCallback(callback);
        sceKernelSleepThreadCB();
    }
    return 0;
}

static void ff_setup_callbacks(void)
{
    SceUID thread = sceKernelCreateThread("Sossy X Huttl callbacks",
                                          ff_callback_thread,
                                          0x11, 0x1000,
                                          PSP_THREAD_ATTR_USER, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

static float ff_analog_axis(unsigned char raw)
{
    const float dead_zone = 0.17f;
    float value = (float)((int)raw - 128) / 127.0f;
    float magnitude;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    magnitude = fabsf(value);
    if (magnitude <= dead_zone) return 0.0f;
    magnitude = (magnitude - dead_zone) / (1.0f - dead_zone);
    magnitude *= magnitude;
    return value < 0.0f ? -magnitude : magnitude;
}

static FFInput ff_read_input(const SceCtrlData *pad, unsigned int pressed)
{
    FFInput input;
    memset(&input, 0, sizeof(input));
    input.move = -ff_analog_axis(pad->Ly);
    input.turn = ff_analog_axis(pad->Lx);
    if (pad->Buttons & PSP_CTRL_UP) input.move += 1.0f;
    if (pad->Buttons & PSP_CTRL_DOWN) input.move -= 1.0f;
    if (pad->Buttons & PSP_CTRL_LEFT) input.turn -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) input.turn += 1.0f;
    if (pad->Buttons & PSP_CTRL_LTRIGGER) input.strafe -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) input.strafe += 1.0f;
    if (input.move > 1.0f) input.move = 1.0f;
    if (input.move < -1.0f) input.move = -1.0f;
    if (input.turn > 1.0f) input.turn = 1.0f;
    if (input.turn < -1.0f) input.turn = -1.0f;
    input.shutter_pressed = (pressed & PSP_CTRL_CROSS) != 0;
    input.confirm_pressed = input.shutter_pressed;
    input.interact_pressed = (pressed & PSP_CTRL_CIRCLE) != 0;
    input.pause_pressed = (pressed & PSP_CTRL_START) != 0;
    return input;
}

static FFTagInput ff_read_tag_input(const SceCtrlData *pad,
                                    unsigned int pressed)
{
    FFTagInput input;
    memset(&input, 0, sizeof(input));
    input.analog_x = ff_analog_axis(pad->Lx);
    input.analog_y = ff_analog_axis(pad->Ly);
    if (pressed & PSP_CTRL_LEFT) --input.dpad_x;
    if (pressed & PSP_CTRL_RIGHT) ++input.dpad_x;
    if (pressed & PSP_CTRL_UP) --input.dpad_y;
    if (pressed & PSP_CTRL_DOWN) ++input.dpad_y;
    input.draw_held = (pad->Buttons & PSP_CTRL_CROSS) != 0;
    input.erase_held = (pad->Buttons & PSP_CTRL_CIRCLE) != 0;
    input.color_pressed = (pressed & PSP_CTRL_SQUARE) != 0;
    input.undo_pressed = (pressed & PSP_CTRL_TRIANGLE) != 0;
    input.clear_held = (pad->Buttons & PSP_CTRL_LTRIGGER) != 0 &&
                       (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
    input.finish_pressed = (pressed & PSP_CTRL_START) != 0;
    input.confirm_pressed = (pressed & PSP_CTRL_CROSS) != 0;
    input.cancel_pressed = (pressed & PSP_CTRL_CIRCLE) != 0;
    return input;
}

static void ff_start_new_run(int *loading_frames)
{
    ff_game_start_run(&g_game, g_game.run_seed + 0x9E3779B9u);
    ff_album_reset(&g_album);
    *loading_frames = FF_LOADING_FRAMES;
    g_leaderboard_notice = NULL;
    g_highlight_rank = -1;
}

static void ff_open_leaderboard(int page, int highlight_rank,
                                const char *notice)
{
    int page_count = ff_leaderboard_page_count(&g_leaderboard);
    if (page < 0) page = 0;
    if (page >= page_count) page = page_count - 1;
    g_leaderboard_page = page;
    g_highlight_rank = highlight_rank;
    g_leaderboard_notice = notice;
    g_game.mode = FF_MODE_LEADERBOARD;
}

static void ff_enter_tag_editor(void)
{
    ff_tag_editor_reset(&g_tag_editor);
    g_tag_wait_for_release = true;
    g_game.mode = FF_MODE_TAG_EDITOR;
}

static void ff_submit_tag(void)
{
    FFTagBitmap tag;
    int rank;
    bool saved;
    ff_tag_pack(g_tag_editor.pixels, &tag);
    rank = ff_leaderboard_insert(&g_leaderboard, g_game.score, &tag);
    if (rank < 0) {
        ff_open_leaderboard(ff_leaderboard_page_count(&g_leaderboard) - 1,
                            -1, "RUN DID NOT PLACE");
        return;
    }
    saved = g_score_path[0] != '\0' &&
            ff_leaderboard_save(&g_leaderboard, g_score_path);
    if (saved) g_load_notice = NULL;
    ff_open_leaderboard(rank / FF_LEADERBOARD_PAGE_SIZE, rank,
                        saved ? "NEW TAG SAVED" :
                                "SAVE FAILED MEMORY ONLY");
}

int main(int argument_count, char *arguments[])
{
    SceCtrlData pad;
    unsigned int previous_buttons = 0;
    uint64_t previous_tick;
    uint32_t tick_resolution;
    float smoothed_frame_seconds = 1.0f / 60.0f;
    int loading_frames = FF_LOADING_FRAMES;
    bool show_debug = false;

    ff_setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    tick_resolution = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&previous_tick);
    ff_game_initialize(&g_game,
                       (uint32_t)previous_tick ^ (uint32_t)(previous_tick >> 32));
    ff_album_reset(&g_album);
    ff_leaderboard_reset(&g_leaderboard);
    g_score_path[0] = '\0';
    if (ff_leaderboard_make_path(g_score_path, sizeof(g_score_path),
                                 argument_count > 0 ? arguments[0] : NULL)) {
        FFLeaderboardLoadStatus load_status =
            ff_leaderboard_load(&g_leaderboard, g_score_path);
        if (load_status == FF_LEADERBOARD_LOAD_INVALID) {
            ff_leaderboard_reset(&g_leaderboard);
            g_load_notice = "SCORE DATA RESET";
        } else if (load_status == FF_LEADERBOARD_LOAD_IO_ERROR) {
            ff_leaderboard_reset(&g_leaderboard);
            g_load_notice = "SCORES UNAVAILABLE";
        }
    } else {
        g_load_notice = "SCORES UNAVAILABLE";
    }
    ff_renderer_initialize();
    ff_audio_initialize();

    while (g_running) {
        uint64_t current_tick;
        float delta_seconds;
        unsigned int pressed;
        FFInput input;
        FFGameMode mode_before;
        bool showing_loading;

        sceRtcGetCurrentTick(&current_tick);
        delta_seconds = (float)(current_tick - previous_tick) /
                        (float)tick_resolution;
        previous_tick = current_tick;
        if (delta_seconds < 0.0f || delta_seconds > 0.25f) {
            delta_seconds = 1.0f / 60.0f;
        }
        smoothed_frame_seconds += (delta_seconds - smoothed_frame_seconds) * 0.08f;

        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) {
            g_running = 0;
            break;
        }
        input = ff_read_input(&pad, pressed);

        mode_before = g_game.mode;
        g_game.pending_events = 0;
        showing_loading = loading_frames > 0;
        if (showing_loading) {
            --loading_frames;
        } else {
            if (g_game.mode == FF_MODE_TITLE) {
                if (pressed & PSP_CTRL_TRIANGLE) {
                    ff_open_leaderboard(0, -1, g_load_notice);
                } else if (pressed & PSP_CTRL_CROSS) {
                    ff_start_new_run(&loading_frames);
                    showing_loading = true;
                }
            } else if (g_game.mode == FF_MODE_RESULTS) {
                if (pressed & PSP_CTRL_CROSS) {
                    if (ff_leaderboard_qualifies(&g_leaderboard,
                                                 g_game.score)) {
                        ff_enter_tag_editor();
                    } else {
                        ff_open_leaderboard(
                            ff_leaderboard_page_count(&g_leaderboard) - 1,
                            -1, "RUN DID NOT PLACE");
                    }
                }
            } else if (g_game.mode == FF_MODE_TAG_EDITOR) {
                const unsigned int tag_buttons =
                    PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE |
                    PSP_CTRL_TRIANGLE | PSP_CTRL_START | PSP_CTRL_LTRIGGER |
                    PSP_CTRL_RTRIGGER;
                if (g_tag_wait_for_release) {
                    if ((pad.Buttons & tag_buttons) == 0) {
                        g_tag_wait_for_release = false;
                    }
                } else {
                    FFTagInput tag_input = ff_read_tag_input(&pad, pressed);
                    if (ff_tag_editor_update(&g_tag_editor, &tag_input,
                                             delta_seconds)) {
                        ff_submit_tag();
                    }
                }
            } else if (g_game.mode == FF_MODE_LEADERBOARD) {
                int page_count = ff_leaderboard_page_count(&g_leaderboard);
                if ((pressed & PSP_CTRL_LTRIGGER) ||
                    (pressed & PSP_CTRL_LEFT)) {
                    if (g_leaderboard_page > 0) --g_leaderboard_page;
                }
                if ((pressed & PSP_CTRL_RTRIGGER) ||
                    (pressed & PSP_CTRL_RIGHT)) {
                    if (g_leaderboard_page + 1 < page_count) {
                        ++g_leaderboard_page;
                    }
                }
                if (pressed & PSP_CTRL_CROSS) {
                    ff_start_new_run(&loading_frames);
                    showing_loading = true;
                } else if (pressed & PSP_CTRL_CIRCLE) {
                    g_game.mode = FF_MODE_TITLE;
                    g_leaderboard_notice = NULL;
                    g_highlight_rank = -1;
                }
            } else {
                if (pressed & PSP_CTRL_TRIANGLE) show_debug = !show_debug;
                ff_game_update(&g_game, &input, delta_seconds);
            }
            if (g_game.pending_events & FF_EVENT_NEXT_FLOOR) {
                loading_frames = FF_LOADING_FRAMES;
                showing_loading = true;
            }
        }

        if (showing_loading) {
            float progress = 1.0f -
                (float)loading_frames / (float)FF_LOADING_FRAMES;
            ff_audio_play_events(g_game.pending_events);
            ff_renderer_render_loading(g_game.floor_number, progress);
        } else if (g_game.mode == FF_MODE_PLAYING ||
                   g_game.mode == FF_MODE_PAUSED) {
            FFPhotoResult photo;
            memset(&photo, 0, sizeof(photo));
            ff_renderer_render_world(&g_game);
            if (mode_before == FF_MODE_PLAYING && input.shutter_pressed &&
                g_game.mode == FF_MODE_PLAYING) {
                photo = ff_game_take_photo(&g_game);
                if (photo.event_flags & FF_EVENT_SHUTTER) {
                    ff_album_consider(&g_album, ff_renderer_pixels(),
                                      FF_RENDER_WIDTH, FF_RENDER_HEIGHT,
                                      ff_renderer_stride(), photo.photo_score,
                                      g_game.floor_number, photo.hit_count,
                                      photo.capture_count);
                }
            }
            ff_audio_play_events(g_game.pending_events);
            ff_renderer_draw_hud(&g_game, show_debug,
                                 1.0f / smoothed_frame_seconds,
                                 smoothed_frame_seconds * 1000.0f);
        } else if (g_game.mode == FF_MODE_TITLE) {
            ff_renderer_render_title();
        } else if (g_game.mode == FF_MODE_RESULTS) {
            ff_audio_play_events(g_game.pending_events);
            ff_renderer_render_results(&g_game, &g_album);
        } else if (g_game.mode == FF_MODE_TAG_EDITOR) {
            ff_renderer_render_tag_editor(&g_tag_editor, g_game.score);
        } else {
            ff_renderer_render_leaderboard(&g_leaderboard,
                                           g_leaderboard_page,
                                           g_highlight_rank,
                                           g_leaderboard_notice);
        }
        ff_renderer_present();
    }

    ff_audio_shutdown();
    ff_renderer_shutdown();
    sceKernelExitGame();
    return 0;
}
