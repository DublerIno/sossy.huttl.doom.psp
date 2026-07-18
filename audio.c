#include "audio.h"

#include "game.h"

#include <pspaudio.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <stdint.h>
#include <string.h>

#define FF_AUDIO_RATE 44100
#define FF_AUDIO_SAMPLES 512
#define FF_AUDIO_VOICES 10

typedef enum {
    FF_SOUND_SHUTTER = 0,
    FF_SOUND_EMPTY,
    FF_SOUND_HIT,
    FF_SOUND_CAPTURE,
    FF_SOUND_PICKUP,
    FF_SOUND_HURT,
    FF_SOUND_DOOR,
    FF_SOUND_TRANSITION,
    FF_SOUND_DEATH
} FFSoundType;

typedef struct {
    FFSoundType type;
    int age;
    int length;
    int volume;
    uint32_t phase;
    uint32_t phase_step;
    uint32_t noise;
    int active;
} FFVoice;

static int16_t g_audio_buffer[FF_AUDIO_SAMPLES * 2] __attribute__((aligned(64)));
static FFVoice g_voices[FF_AUDIO_VOICES];
static volatile int g_audio_running;
static int g_audio_channel = -1;
static SceUID g_audio_thread = -1;
static SceUID g_audio_mutex = -1;
static int g_pending_events;
static uint32_t g_ambient_phase;
static uint32_t g_ambient_phase_two;

static uint32_t ff_frequency_step(unsigned int frequency)
{
    return (uint32_t)(((uint64_t)frequency << 32) / FF_AUDIO_RATE);
}

static int ff_triangle(uint32_t phase)
{
    int value = (int)((phase >> 16) & 0xffffu);
    if (value >= 32768) value = 65535 - value;
    return value * 2 - 32767;
}

static int ff_noise(FFVoice *voice)
{
    uint32_t value = voice->noise;
    if (value == 0) value = 0xa341316cu;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    voice->noise = value;
    return (int)(value >> 16) - 32768;
}

static void ff_start_voice(FFSoundType type, int length_ms,
                           unsigned int frequency, int volume)
{
    int index = -1;
    int i;
    for (i = 0; i < FF_AUDIO_VOICES; ++i) {
        if (!g_voices[i].active) {
            index = i;
            break;
        }
    }
    if (index < 0) index = FF_AUDIO_VOICES - 1;
    memset(&g_voices[index], 0, sizeof(g_voices[index]));
    g_voices[index].type = type;
    g_voices[index].length = length_ms * FF_AUDIO_RATE / 1000;
    g_voices[index].volume = volume;
    g_voices[index].phase_step = ff_frequency_step(frequency);
    g_voices[index].noise = 0x8f31a2d5u ^ (uint32_t)(type * 0x9e3779b9u) ^
                            (uint32_t)g_ambient_phase;
    g_voices[index].active = 1;
}

static void ff_dispatch_events(int events)
{
    if (events & FF_EVENT_SHUTTER) {
        ff_start_voice(FF_SOUND_SHUTTER, 170, 74, 12500);
    }
    if (events & FF_EVENT_EMPTY) {
        ff_start_voice(FF_SOUND_EMPTY, 90, 1850, 7000);
    }
    if (events & FF_EVENT_HIT) {
        ff_start_voice(FF_SOUND_HIT, 280, 230, 8000);
    }
    if (events & FF_EVENT_CAPTURE) {
        ff_start_voice(FF_SOUND_CAPTURE, 650, 360, 9500);
    }
    if (events & (FF_EVENT_FILM | FF_EVENT_HEAL)) {
        ff_start_voice(FF_SOUND_PICKUP, 360, 660, 7000);
    }
    if (events & FF_EVENT_HURT) {
        ff_start_voice(FF_SOUND_HURT, 260, 66, 9000);
    }
    if (events & FF_EVENT_DOOR) {
        ff_start_voice(FF_SOUND_DOOR, 390, 48, 7000);
    }
    if (events & FF_EVENT_NEXT_FLOOR) {
        ff_start_voice(FF_SOUND_TRANSITION, 900, 96, 8500);
    }
    if (events & FF_EVENT_DEATH) {
        ff_start_voice(FF_SOUND_DEATH, 1300, 82, 10000);
    }
}

static int ff_voice_sample(FFVoice *voice)
{
    int sample = 0;
    int remaining = voice->length - voice->age;
    int envelope;
    if (remaining <= 0) {
        voice->active = 0;
        return 0;
    }
    envelope = remaining * 256 / voice->length;
    switch (voice->type) {
    case FF_SOUND_SHUTTER:
        if (voice->age < FF_AUDIO_RATE / 90) {
            sample = ff_noise(voice);
        } else {
            sample = ff_triangle(voice->phase);
        }
        break;
    case FF_SOUND_EMPTY:
        sample = (voice->phase & 0x80000000u) ? 22000 : -22000;
        if ((voice->age / 180) & 1) sample = 0;
        break;
    case FF_SOUND_HIT:
        sample = ff_noise(voice) / 2 + ff_triangle(voice->phase) / 2;
        voice->phase_step += 2100u;
        break;
    case FF_SOUND_CAPTURE:
        sample = ff_triangle(voice->phase);
        voice->phase_step += 6200u;
        break;
    case FF_SOUND_PICKUP:
        sample = ff_triangle(voice->phase);
        if (voice->age > voice->length / 2) {
            voice->phase_step = ff_frequency_step(880);
        }
        break;
    case FF_SOUND_HURT:
        sample = ff_noise(voice) / 2 +
                 ((voice->phase & 0x80000000u) ? 10000 : -10000);
        break;
    case FF_SOUND_DOOR:
        sample = ff_noise(voice) / 3 + ff_triangle(voice->phase) / 3;
        break;
    case FF_SOUND_TRANSITION:
        sample = ff_triangle(voice->phase) + ff_noise(voice) / 6;
        voice->phase_step += 1400u;
        break;
    case FF_SOUND_DEATH:
        sample = ff_triangle(voice->phase) + ff_noise(voice) / 5;
        if (voice->phase_step > 18000u) voice->phase_step -= 18000u;
        break;
    default:
        break;
    }
    voice->phase += voice->phase_step;
    ++voice->age;
    return sample * voice->volume / 32768 * envelope / 256;
}

static int ff_audio_thread(SceSize argument_size, void *argument)
{
    (void)argument_size;
    (void)argument;
    while (g_audio_running) {
        int events = 0;
        int sample_index;
        if (g_audio_mutex >= 0) {
            sceKernelWaitSema(g_audio_mutex, 1, NULL);
            events = g_pending_events;
            g_pending_events = 0;
            sceKernelSignalSema(g_audio_mutex, 1);
        }
        ff_dispatch_events(events);

        for (sample_index = 0; sample_index < FF_AUDIO_SAMPLES; ++sample_index) {
            int left;
            int right;
            int voice_index;
            int ambient = ff_triangle(g_ambient_phase) / 110 +
                          ff_triangle(g_ambient_phase_two) / 170;
            g_ambient_phase += ff_frequency_step(43);
            g_ambient_phase_two += ff_frequency_step(59);
            left = ambient;
            right = -ambient / 2;
            for (voice_index = 0; voice_index < FF_AUDIO_VOICES; ++voice_index) {
                if (g_voices[voice_index].active) {
                    int value = ff_voice_sample(&g_voices[voice_index]);
                    left += value;
                    right += value;
                }
            }
            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;
            g_audio_buffer[sample_index * 2] = (int16_t)left;
            g_audio_buffer[sample_index * 2 + 1] = (int16_t)right;
        }
        sceAudioOutputPannedBlocking(g_audio_channel,
                                     PSP_AUDIO_VOLUME_MAX,
                                     PSP_AUDIO_VOLUME_MAX,
                                     g_audio_buffer);
    }
    return 0;
}

void ff_audio_initialize(void)
{
    memset(g_voices, 0, sizeof(g_voices));
    g_pending_events = 0;
    g_audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                        FF_AUDIO_SAMPLES,
                                        PSP_AUDIO_FORMAT_STEREO);
    if (g_audio_channel < 0) return;
    g_audio_mutex = sceKernelCreateSema("Sossy X Huttl audio mutex", 0, 1, 1,
                                        NULL);
    if (g_audio_mutex < 0) {
        sceAudioChRelease(g_audio_channel);
        g_audio_channel = -1;
        return;
    }
    g_audio_running = 1;
    g_audio_thread = sceKernelCreateThread("Sossy X Huttl audio",
                                           ff_audio_thread,
                                           0x12, 0x4000,
                                           PSP_THREAD_ATTR_USER, NULL);
    if (g_audio_thread < 0 ||
        sceKernelStartThread(g_audio_thread, 0, NULL) < 0) {
        g_audio_running = 0;
        sceKernelDeleteSema(g_audio_mutex);
        g_audio_mutex = -1;
        sceAudioChRelease(g_audio_channel);
        g_audio_channel = -1;
        g_audio_thread = -1;
    }
}

void ff_audio_play_events(int event_flags)
{
    if (g_audio_mutex < 0 || event_flags == 0) return;
    sceKernelWaitSema(g_audio_mutex, 1, NULL);
    g_pending_events |= event_flags;
    sceKernelSignalSema(g_audio_mutex, 1);
}

void ff_audio_shutdown(void)
{
    if (g_audio_channel < 0) return;
    g_audio_running = 0;
    if (g_audio_thread >= 0) {
        sceKernelWaitThreadEnd(g_audio_thread, NULL);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }
    sceAudioChRelease(g_audio_channel);
    g_audio_channel = -1;
    if (g_audio_mutex >= 0) {
        sceKernelDeleteSema(g_audio_mutex);
        g_audio_mutex = -1;
    }
}
