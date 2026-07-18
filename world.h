#ifndef FLASHFRAME_WORLD_H
#define FLASHFRAME_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#define FF_MAP_SIZE 32
#define FF_MAP_CELLS (FF_MAP_SIZE * FF_MAP_SIZE)
#define FF_MAX_ENEMIES 28
#define FF_MAX_PICKUPS 20

typedef enum {
    FF_TILE_FLOOR = 0,
    FF_TILE_WALL_PLASTER = 1,
    FF_TILE_WALL_BRICK = 2,
    FF_TILE_WALL_PANEL = 3,
    FF_TILE_WALL_RITUAL = 4,
    FF_TILE_DOOR = 5,
    FF_TILE_EXIT = 6
} FFTile;

typedef enum {
    FF_ENEMY_IDLE = 0,
    FF_ENEMY_WALK,
    FF_ENEMY_ATTACK,
    FF_ENEMY_HURT,
    FF_ENEMY_CAPTURED
} FFEnemyState;

typedef enum {
    FF_PICKUP_FILM = 0,
    FF_PICKUP_HEALTH
} FFPickupType;

typedef struct {
    float x;
    float y;
    float health;
    float speed;
    float anim_time;
    float state_time;
    float attack_cooldown;
    float wander_angle;
    FFEnemyState state;
    bool active;
} FFEnemy;

typedef struct {
    float x;
    float y;
    FFPickupType type;
    bool active;
} FFPickup;

typedef struct {
    uint8_t tiles[FF_MAP_CELLS];
    int16_t navigation[FF_MAP_CELLS];
    FFEnemy enemies[FF_MAX_ENEMIES];
    FFPickup pickups[FF_MAX_PICKUPS];
    uint32_t seed;
    uint32_t rng_state;
    int floor_number;
    int enemy_count;
    int pickup_count;
    float start_x;
    float start_y;
    float exit_x;
    float exit_y;
} FFWorld;

enum {
    FF_INTERACT_NONE = 0,
    FF_INTERACT_DOOR = 1,
    FF_INTERACT_EXIT = 2
};

void ff_world_generate(FFWorld *world, uint32_t seed, int floor_number);
bool ff_world_in_bounds(int x, int y);
bool ff_world_is_solid(const FFWorld *world, int x, int y);
bool ff_world_is_walkable(const FFWorld *world, int x, int y, bool doors_passable);
bool ff_world_circle_collides(const FFWorld *world, float x, float y, float radius);
void ff_world_move_circle(const FFWorld *world, float *x, float *y,
                          float dx, float dy, float radius);
bool ff_world_has_line_of_sight(const FFWorld *world,
                                float from_x, float from_y,
                                float to_x, float to_y);
void ff_world_refresh_navigation(FFWorld *world, float target_x, float target_y);
int ff_world_update_enemies(FFWorld *world, float player_x, float player_y,
                            float delta_seconds);
int ff_world_interact(FFWorld *world, float player_x, float player_y,
                      float facing_angle);
int ff_world_reachable_count(const FFWorld *world, bool doors_passable);
int ff_world_active_enemy_count(const FFWorld *world);

#endif
