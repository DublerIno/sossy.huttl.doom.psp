#include "album.h"
#include "audio.h"
#include "game.h"
#include "renderer.h"

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

int main(int argument_count, char *arguments[])
{
    SceCtrlData pad;
    unsigned int previous_buttons = 0;
    uint64_t previous_tick;
    uint32_t tick_resolution;
    float smoothed_frame_seconds = 1.0f / 60.0f;
    int loading_frames = FF_LOADING_FRAMES;
    bool show_debug = false;

    (void)argument_count;
    (void)arguments;
    ff_setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    tick_resolution = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&previous_tick);
    ff_game_initialize(&g_game,
                       (uint32_t)previous_tick ^ (uint32_t)(previous_tick >> 32));
    ff_album_reset(&g_album);
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
        if (pressed & PSP_CTRL_TRIANGLE) show_debug = !show_debug;
        input = ff_read_input(&pad, pressed);

        mode_before = g_game.mode;
        g_game.pending_events = 0;
        showing_loading = loading_frames > 0;
        if (showing_loading) {
            --loading_frames;
        } else {
            ff_game_update(&g_game, &input, delta_seconds);
            if (mode_before != FF_MODE_PLAYING &&
                mode_before != FF_MODE_PAUSED &&
                g_game.mode == FF_MODE_PLAYING) {
                ff_album_reset(&g_album);
                loading_frames = FF_LOADING_FRAMES;
                showing_loading = true;
            } else if (g_game.pending_events & FF_EVENT_NEXT_FLOOR) {
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
        } else {
            ff_audio_play_events(g_game.pending_events);
            ff_renderer_render_results(&g_game, &g_album);
        }
        ff_renderer_present();
    }

    ff_audio_shutdown();
    ff_renderer_shutdown();
    sceKernelExitGame();
    return 0;
}
