#ifndef FLASHFRAME_GAME_H
#define FLASHFRAME_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

#define FF_MAX_HEALTH 100
#define FF_MAX_FILM 24
#define FF_START_FILM 12

#define FF_FOV_RADIANS 1.0471975512f
#define FF_PHOTO_HALF_ANGLE 0.2792526803f
#define FF_PHOTO_MAX_DISTANCE 12.0f

typedef enum {
    FF_MODE_TITLE = 0,
    FF_MODE_PLAYING,
    FF_MODE_PAUSED,
    FF_MODE_RESULTS,
    FF_MODE_TAG_EDITOR,
    FF_MODE_LEADERBOARD
} FFGameMode;

typedef struct {
    float move;
    float turn;
    float strafe;
    bool shutter_pressed;
    bool interact_pressed;
    bool pause_pressed;
    bool confirm_pressed;
} FFInput;

enum {
    FF_EVENT_NONE = 0,
    FF_EVENT_SHUTTER = 1 << 0,
    FF_EVENT_EMPTY = 1 << 1,
    FF_EVENT_HIT = 1 << 2,
    FF_EVENT_CAPTURE = 1 << 3,
    FF_EVENT_FILM = 1 << 4,
    FF_EVENT_HEAL = 1 << 5,
    FF_EVENT_HURT = 1 << 6,
    FF_EVENT_DOOR = 1 << 7,
    FF_EVENT_NEXT_FLOOR = 1 << 8,
    FF_EVENT_DEATH = 1 << 9
};

typedef struct {
    int event_flags;
    int hit_count;
    int capture_count;
    int damage_dealt;
    int photo_score;
    float best_alignment;
} FFPhotoResult;

typedef struct {
    FFWorld world;
    FFGameMode mode;
    uint32_t run_seed;
    int floor_number;
    int health;
    int film;
    int score;
    int total_captures;
    int photographs_taken;
    float player_x;
    float player_y;
    float player_angle;
    float flash_cooldown;
    float flash_amount;
    float elapsed_seconds;
    int pending_events;
} FFGame;

void ff_game_initialize(FFGame *game, uint32_t seed);
void ff_game_start_run(FFGame *game, uint32_t seed);
void ff_game_update(FFGame *game, const FFInput *input, float delta_seconds);
FFPhotoResult ff_game_take_photo(FFGame *game);
float ff_game_wrap_angle(float angle);
float ff_game_angle_difference(float a, float b);

#endif
