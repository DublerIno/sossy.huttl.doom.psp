#include "game.h"

#include <math.h>
#include <string.h>

#define FF_PI 3.14159265358979323846f
#define FF_TWO_PI (2.0f * FF_PI)

static uint32_t ff_mix_seed(uint32_t seed, int floor_number)
{
    uint32_t value = seed ^ ((uint32_t)floor_number * 0x9E3779B9u);
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

float ff_game_wrap_angle(float angle)
{
    while (angle > FF_PI) {
        angle -= FF_TWO_PI;
    }
    while (angle < -FF_PI) {
        angle += FF_TWO_PI;
    }
    return angle;
}

float ff_game_angle_difference(float a, float b)
{
    return ff_game_wrap_angle(a - b);
}

void ff_game_initialize(FFGame *game, uint32_t seed)
{
    memset(game, 0, sizeof(*game));
    game->run_seed = seed != 0 ? seed : 0xF1A5F00Du;
    game->mode = FF_MODE_TITLE;
}

void ff_game_start_run(FFGame *game, uint32_t seed)
{
    memset(game, 0, sizeof(*game));
    game->run_seed = seed != 0 ? seed : 0xF1A5F00Du;
    game->floor_number = 1;
    game->health = FF_MAX_HEALTH;
    game->film = FF_START_FILM;
    game->player_angle = 0.0f;
    game->mode = FF_MODE_PLAYING;
    ff_world_generate(&game->world, ff_mix_seed(game->run_seed, 1), 1);
    game->player_x = game->world.start_x;
    game->player_y = game->world.start_y;
}

static void ff_game_advance_floor(FFGame *game)
{
    ++game->floor_number;
    game->score += 250 * game->floor_number;
    ff_world_generate(&game->world,
                      ff_mix_seed(game->run_seed, game->floor_number),
                      game->floor_number);
    game->player_x = game->world.start_x;
    game->player_y = game->world.start_y;
    game->player_angle = 0.0f;
    game->flash_cooldown = 0.0f;
    game->pending_events |= FF_EVENT_NEXT_FLOOR;
}

static void ff_game_collect_pickups(FFGame *game)
{
    int i;
    for (i = 0; i < game->world.pickup_count; ++i) {
        FFPickup *pickup = &game->world.pickups[i];
        float dx;
        float dy;
        if (!pickup->active) {
            continue;
        }
        dx = pickup->x - game->player_x;
        dy = pickup->y - game->player_y;
        if (dx * dx + dy * dy > 0.24f) {
            continue;
        }

        if (pickup->type == FF_PICKUP_FILM && game->film < FF_MAX_FILM) {
            game->film += 6;
            if (game->film > FF_MAX_FILM) {
                game->film = FF_MAX_FILM;
            }
            pickup->active = false;
            game->score += 50;
            game->pending_events |= FF_EVENT_FILM;
        } else if (pickup->type == FF_PICKUP_HEALTH && game->health < FF_MAX_HEALTH) {
            game->health += 25;
            if (game->health > FF_MAX_HEALTH) {
                game->health = FF_MAX_HEALTH;
            }
            pickup->active = false;
            game->score += 50;
            game->pending_events |= FF_EVENT_HEAL;
        }
    }
}

void ff_game_update(FFGame *game, const FFInput *input, float delta_seconds)
{
    float direction_x;
    float direction_y;
    float right_x;
    float right_y;
    float move_x;
    float move_y;
    int damage;

    if (delta_seconds < 0.0f) {
        delta_seconds = 0.0f;
    } else if (delta_seconds > 0.05f) {
        delta_seconds = 0.05f;
    }

    if (game->mode != FF_MODE_PLAYING && game->mode != FF_MODE_PAUSED) return;
    if (input->pause_pressed) {
        game->mode = game->mode == FF_MODE_PAUSED ? FF_MODE_PLAYING : FF_MODE_PAUSED;
    }
    if (game->mode != FF_MODE_PLAYING) {
        return;
    }

    game->elapsed_seconds += delta_seconds;
    if (game->flash_cooldown > 0.0f) {
        game->flash_cooldown -= delta_seconds;
        if (game->flash_cooldown < 0.0f) {
            game->flash_cooldown = 0.0f;
        }
    }
    if (game->flash_amount > 0.0f) {
        game->flash_amount -= delta_seconds * 5.5f;
        if (game->flash_amount < 0.0f) {
            game->flash_amount = 0.0f;
        }
    }

    game->player_angle = ff_game_wrap_angle(
        game->player_angle + input->turn * 2.25f * delta_seconds
    );
    direction_x = cosf(game->player_angle);
    direction_y = sinf(game->player_angle);
    right_x = -direction_y;
    right_y = direction_x;
    move_x = direction_x * input->move * 2.45f * delta_seconds +
             right_x * input->strafe * 2.05f * delta_seconds;
    move_y = direction_y * input->move * 2.45f * delta_seconds +
             right_y * input->strafe * 2.05f * delta_seconds;
    ff_world_move_circle(&game->world, &game->player_x, &game->player_y,
                         move_x, move_y, 0.22f);

    if (input->interact_pressed) {
        int interaction = ff_world_interact(&game->world, game->player_x,
                                            game->player_y, game->player_angle);
        if (interaction == FF_INTERACT_DOOR) {
            game->pending_events |= FF_EVENT_DOOR;
        } else if (interaction == FF_INTERACT_EXIT) {
            ff_game_advance_floor(game);
            return;
        }
    }

    ff_game_collect_pickups(game);
    damage = ff_world_update_enemies(&game->world, game->player_x,
                                     game->player_y, delta_seconds);
    if (damage > 0) {
        game->health -= damage;
        game->pending_events |= FF_EVENT_HURT;
        if (game->health <= 0) {
            game->health = 0;
            game->mode = FF_MODE_RESULTS;
            game->pending_events |= FF_EVENT_DEATH;
        }
    }
}

FFPhotoResult ff_game_take_photo(FFGame *game)
{
    FFPhotoResult result;
    int i;
    memset(&result, 0, sizeof(result));

    if (game->mode != FF_MODE_PLAYING || game->flash_cooldown > 0.0f) {
        return result;
    }
    if (game->film <= 0) {
        result.event_flags = FF_EVENT_EMPTY;
        game->pending_events |= FF_EVENT_EMPTY;
        game->flash_cooldown = 0.18f;
        return result;
    }

    --game->film;
    ++game->photographs_taken;
    game->flash_cooldown = 0.70f;
    game->flash_amount = 1.0f;
    result.event_flags = FF_EVENT_SHUTTER;

    for (i = 0; i < game->world.enemy_count; ++i) {
        FFEnemy *enemy = &game->world.enemies[i];
        float dx;
        float dy;
        float distance;
        float target_angle;
        float error;
        float alignment;
        float distance_factor;
        int damage;

        if (!enemy->active || enemy->state == FF_ENEMY_CAPTURED) {
            continue;
        }
        dx = enemy->x - game->player_x;
        dy = enemy->y - game->player_y;
        distance = sqrtf(dx * dx + dy * dy);
        if (distance > FF_PHOTO_MAX_DISTANCE || distance < 0.05f) {
            continue;
        }
        target_angle = atan2f(dy, dx);
        error = fabsf(ff_game_angle_difference(target_angle, game->player_angle));
        if (error > FF_PHOTO_HALF_ANGLE) {
            continue;
        }
        if (!ff_world_has_line_of_sight(&game->world, game->player_x,
                                        game->player_y, enemy->x, enemy->y)) {
            continue;
        }

        alignment = 1.0f - error / FF_PHOTO_HALF_ANGLE;
        distance_factor = 1.0f - fmaxf(0.0f, distance - 1.0f) / 12.0f;
        if (distance_factor < 0.15f) {
            distance_factor = 0.15f;
        }
        damage = (int)(30.0f + 70.0f * alignment * distance_factor + 0.5f);
        enemy->health -= (float)damage;
        enemy->state = FF_ENEMY_HURT;
        enemy->state_time = 0.22f;
        ++result.hit_count;
        result.damage_dealt += damage;
        if (alignment > result.best_alignment) {
            result.best_alignment = alignment;
        }

        if (enemy->health <= 0.0f) {
            enemy->health = 0.0f;
            enemy->state = FF_ENEMY_CAPTURED;
            enemy->state_time = 0.72f;
            ++result.capture_count;
            ++game->total_captures;
        }
    }

    if (result.hit_count > 0) {
        result.event_flags |= FF_EVENT_HIT;
    }
    if (result.capture_count > 0) {
        result.event_flags |= FF_EVENT_CAPTURE;
    }
    if (result.hit_count > 1) {
        result.photo_score += 250 * (result.hit_count - 1);
    }
    result.photo_score += result.damage_dealt * 5 + result.capture_count * 750;
    game->score += result.photo_score;
    game->pending_events |= result.event_flags;
    return result;
}
