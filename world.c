#include "world.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FF_MAX_ROOMS 10

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int center_x;
    int center_y;
} FFRoom;

static int ff_index(int x, int y)
{
    return y * FF_MAP_SIZE + x;
}

static uint32_t ff_rng_next(FFWorld *world)
{
    uint32_t value = world->rng_state;
    if (value == 0) {
        value = 0xA341316Cu;
    }
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    world->rng_state = value;
    return value;
}

static int ff_rng_range(FFWorld *world, int minimum, int maximum)
{
    uint32_t span = (uint32_t)(maximum - minimum + 1);
    return minimum + (int)(ff_rng_next(world) % span);
}

static float ff_rng_unit(FFWorld *world)
{
    return (float)(ff_rng_next(world) & 0x00FFFFFFu) / 16777215.0f;
}

bool ff_world_in_bounds(int x, int y)
{
    return x >= 0 && y >= 0 && x < FF_MAP_SIZE && y < FF_MAP_SIZE;
}

bool ff_world_is_solid(const FFWorld *world, int x, int y)
{
    uint8_t tile;
    if (!ff_world_in_bounds(x, y)) {
        return true;
    }
    tile = world->tiles[ff_index(x, y)];
    return tile >= FF_TILE_WALL_PLASTER && tile <= FF_TILE_DOOR;
}

bool ff_world_is_walkable(const FFWorld *world, int x, int y, bool doors_passable)
{
    uint8_t tile;
    if (!ff_world_in_bounds(x, y)) {
        return false;
    }
    tile = world->tiles[ff_index(x, y)];
    if (tile == FF_TILE_FLOOR || tile == FF_TILE_EXIT) {
        return true;
    }
    return doors_passable && tile == FF_TILE_DOOR;
}

static uint8_t ff_wall_variant(uint32_t seed, int x, int y)
{
    uint32_t hash = seed ^ ((uint32_t)x * 0x9E3779B9u) ^ ((uint32_t)y * 0x85EBCA6Bu);
    hash ^= hash >> 16;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15;
    return (uint8_t)(FF_TILE_WALL_PLASTER + (hash & 3u));
}

static void ff_fill_walls(FFWorld *world)
{
    int y;
    for (y = 0; y < FF_MAP_SIZE; ++y) {
        int x;
        for (x = 0; x < FF_MAP_SIZE; ++x) {
            world->tiles[ff_index(x, y)] = ff_wall_variant(world->seed, x, y);
        }
    }
}

static void ff_carve_cell(FFWorld *world, int x, int y)
{
    if (x > 0 && y > 0 && x < FF_MAP_SIZE - 1 && y < FF_MAP_SIZE - 1) {
        world->tiles[ff_index(x, y)] = FF_TILE_FLOOR;
    }
}

static void ff_carve_room(FFWorld *world, const FFRoom *room)
{
    int y;
    for (y = room->y; y < room->y + room->h; ++y) {
        int x;
        for (x = room->x; x < room->x + room->w; ++x) {
            ff_carve_cell(world, x, y);
        }
    }
}

static bool ff_rooms_overlap(const FFRoom *a, const FFRoom *b)
{
    return a->x - 1 < b->x + b->w + 1 &&
           a->x + a->w + 1 > b->x - 1 &&
           a->y - 1 < b->y + b->h + 1 &&
           a->y + a->h + 1 > b->y - 1;
}

static void ff_carve_horizontal(FFWorld *world, int from_x, int to_x, int y)
{
    int step = from_x <= to_x ? 1 : -1;
    int x = from_x;
    for (;;) {
        ff_carve_cell(world, x, y);
        if (x == to_x) {
            break;
        }
        x += step;
    }
}

static void ff_carve_vertical(FFWorld *world, int from_y, int to_y, int x)
{
    int step = from_y <= to_y ? 1 : -1;
    int y = from_y;
    for (;;) {
        ff_carve_cell(world, x, y);
        if (y == to_y) {
            break;
        }
        y += step;
    }
}

static void ff_connect_rooms(FFWorld *world, const FFRoom *a, const FFRoom *b)
{
    if (ff_rng_next(world) & 1u) {
        ff_carve_horizontal(world, a->center_x, b->center_x, a->center_y);
        ff_carve_vertical(world, a->center_y, b->center_y, b->center_x);
    } else {
        ff_carve_vertical(world, a->center_y, b->center_y, a->center_x);
        ff_carve_horizontal(world, a->center_x, b->center_x, b->center_y);
    }
}

static int ff_make_rooms(FFWorld *world, FFRoom rooms[FF_MAX_ROOMS])
{
    int target = ff_rng_range(world, 7, FF_MAX_ROOMS);
    int count = 0;
    int attempt;

    for (attempt = 0; attempt < 180 && count < target; ++attempt) {
        FFRoom candidate;
        bool valid = true;
        int i;

        candidate.w = ff_rng_range(world, 4, 8);
        candidate.h = ff_rng_range(world, 4, 7);
        candidate.x = ff_rng_range(world, 2, FF_MAP_SIZE - candidate.w - 3);
        candidate.y = ff_rng_range(world, 2, FF_MAP_SIZE - candidate.h - 3);
        candidate.center_x = candidate.x + candidate.w / 2;
        candidate.center_y = candidate.y + candidate.h / 2;

        for (i = 0; i < count; ++i) {
            if (ff_rooms_overlap(&candidate, &rooms[i])) {
                valid = false;
                break;
            }
        }

        if (valid) {
            rooms[count++] = candidate;
            ff_carve_room(world, &candidate);
        }
    }

    if (count < 4) {
        static const FFRoom fallback[4] = {
            {3, 3, 6, 6, 6, 6},
            {22, 3, 6, 6, 25, 6},
            {3, 22, 6, 6, 6, 25},
            {22, 22, 6, 6, 25, 25}
        };
        ff_fill_walls(world);
        count = 4;
        memcpy(rooms, fallback, sizeof(fallback));
        for (attempt = 0; attempt < count; ++attempt) {
            ff_carve_room(world, &rooms[attempt]);
        }
    }

    return count;
}

static void ff_compute_distances(const FFWorld *world, int start_x, int start_y,
                                 bool doors_passable, int16_t distances[FF_MAP_CELLS])
{
    int16_t queue_x[FF_MAP_CELLS];
    int16_t queue_y[FF_MAP_CELLS];
    int head = 0;
    int tail = 0;
    int i;
    static const int directions[4][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };

    for (i = 0; i < FF_MAP_CELLS; ++i) {
        distances[i] = -1;
    }

    if (!ff_world_is_walkable(world, start_x, start_y, doors_passable)) {
        return;
    }

    distances[ff_index(start_x, start_y)] = 0;
    queue_x[tail] = (int16_t)start_x;
    queue_y[tail] = (int16_t)start_y;
    ++tail;

    while (head < tail) {
        int x = queue_x[head];
        int y = queue_y[head];
        int16_t next_distance = (int16_t)(distances[ff_index(x, y)] + 1);
        int direction;
        ++head;

        for (direction = 0; direction < 4; ++direction) {
            int nx = x + directions[direction][0];
            int ny = y + directions[direction][1];
            int index;
            if (!ff_world_is_walkable(world, nx, ny, doors_passable)) {
                continue;
            }
            index = ff_index(nx, ny);
            if (distances[index] >= 0) {
                continue;
            }
            distances[index] = next_distance;
            queue_x[tail] = (int16_t)nx;
            queue_y[tail] = (int16_t)ny;
            ++tail;
        }
    }
}

static void ff_place_doors(FFWorld *world, int desired_count, int start_x, int start_y)
{
    int placed = 0;
    int attempt;
    for (attempt = 0; attempt < 300 && placed < desired_count; ++attempt) {
        int x = ff_rng_range(world, 2, FF_MAP_SIZE - 3);
        int y = ff_rng_range(world, 2, FF_MAP_SIZE - 3);
        bool horizontal;
        bool vertical;

        if (world->tiles[ff_index(x, y)] != FF_TILE_FLOOR) {
            continue;
        }
        if (abs(x - start_x) + abs(y - start_y) < 4) {
            continue;
        }

        horizontal = ff_world_is_walkable(world, x - 1, y, true) &&
                     ff_world_is_walkable(world, x + 1, y, true) &&
                     !ff_world_is_walkable(world, x, y - 1, true) &&
                     !ff_world_is_walkable(world, x, y + 1, true);
        vertical = ff_world_is_walkable(world, x, y - 1, true) &&
                   ff_world_is_walkable(world, x, y + 1, true) &&
                   !ff_world_is_walkable(world, x - 1, y, true) &&
                   !ff_world_is_walkable(world, x + 1, y, true);

        if (horizontal || vertical) {
            world->tiles[ff_index(x, y)] = FF_TILE_DOOR;
            ++placed;
        }
    }
}

static bool ff_position_is_free(const FFWorld *world, float x, float y)
{
    int i;
    for (i = 0; i < world->enemy_count; ++i) {
        const FFEnemy *enemy = &world->enemies[i];
        float dx;
        float dy;
        if (!enemy->active) {
            continue;
        }
        dx = enemy->x - x;
        dy = enemy->y - y;
        if (dx * dx + dy * dy < 0.8f) {
            return false;
        }
    }
    for (i = 0; i < world->pickup_count; ++i) {
        const FFPickup *pickup = &world->pickups[i];
        float dx;
        float dy;
        if (!pickup->active) {
            continue;
        }
        dx = pickup->x - x;
        dy = pickup->y - y;
        if (dx * dx + dy * dy < 0.5f) {
            return false;
        }
    }
    return true;
}

static bool ff_random_spawn_cell(FFWorld *world, int minimum_start_distance,
                                 float *out_x, float *out_y)
{
    int attempt;
    for (attempt = 0; attempt < 500; ++attempt) {
        int x = ff_rng_range(world, 1, FF_MAP_SIZE - 2);
        int y = ff_rng_range(world, 1, FF_MAP_SIZE - 2);
        int distance;
        float px;
        float py;

        if (world->tiles[ff_index(x, y)] != FF_TILE_FLOOR) {
            continue;
        }
        distance = world->navigation[ff_index(x, y)];
        if (distance < minimum_start_distance) {
            continue;
        }
        px = (float)x + 0.5f;
        py = (float)y + 0.5f;
        if (!ff_position_is_free(world, px, py)) {
            continue;
        }
        *out_x = px;
        *out_y = py;
        return true;
    }
    return false;
}

static void ff_spawn_entities(FFWorld *world)
{
    int requested_enemies = 4 + 2 * (world->floor_number - 1);
    int film_pickups = 6 - (world->floor_number - 1) / 4;
    int health_pickups = 3 - (world->floor_number - 1) / 6;
    int i;

    if (requested_enemies > FF_MAX_ENEMIES) {
        requested_enemies = FF_MAX_ENEMIES;
    }
    if (film_pickups < 2) {
        film_pickups = 2;
    }
    if (health_pickups < 1) {
        health_pickups = 1;
    }

    world->enemy_count = 0;
    world->pickup_count = 0;
    memset(world->enemies, 0, sizeof(world->enemies));
    memset(world->pickups, 0, sizeof(world->pickups));

    for (i = 0; i < requested_enemies; ++i) {
        float x;
        float y;
        if (ff_random_spawn_cell(world, 6, &x, &y)) {
            FFEnemy *enemy = &world->enemies[world->enemy_count++];
            enemy->x = x;
            enemy->y = y;
            enemy->health = 100.0f + 10.0f * (float)(world->floor_number - 1);
            enemy->speed = 0.72f * fminf(1.6f, 1.0f + 0.02f * (float)(world->floor_number - 1));
            enemy->wander_angle = ff_rng_unit(world) * 6.2831853072f;
            enemy->state = FF_ENEMY_IDLE;
            enemy->active = true;
        }
    }

    for (i = 0; i < film_pickups + health_pickups && i < FF_MAX_PICKUPS; ++i) {
        float x;
        float y;
        if (ff_random_spawn_cell(world, 3, &x, &y)) {
            FFPickup *pickup = &world->pickups[world->pickup_count++];
            pickup->x = x;
            pickup->y = y;
            pickup->type = i < film_pickups ? FF_PICKUP_FILM : FF_PICKUP_HEALTH;
            pickup->active = true;
        }
    }
}

void ff_world_generate(FFWorld *world, uint32_t seed, int floor_number)
{
    FFRoom rooms[FF_MAX_ROOMS];
    int room_count;
    int i;
    int farthest_index = -1;
    int farthest_distance = -1;

    memset(world, 0, sizeof(*world));
    world->seed = seed;
    world->rng_state = seed ^ ((uint32_t)floor_number * 0x9E3779B9u);
    world->floor_number = floor_number;
    ff_fill_walls(world);
    room_count = ff_make_rooms(world, rooms);

    for (i = 1; i < room_count; ++i) {
        ff_connect_rooms(world, &rooms[i - 1], &rooms[i]);
    }
    for (i = 0; i < 2 && room_count > 2; ++i) {
        int a = ff_rng_range(world, 0, room_count - 1);
        int b = ff_rng_range(world, 0, room_count - 1);
        if (a != b) {
            ff_connect_rooms(world, &rooms[a], &rooms[b]);
        }
    }

    world->start_x = (float)rooms[0].center_x + 0.5f;
    world->start_y = (float)rooms[0].center_y + 0.5f;
    ff_place_doors(world, 2 + floor_number / 5, rooms[0].center_x, rooms[0].center_y);

    ff_compute_distances(world, rooms[0].center_x, rooms[0].center_y,
                         true, world->navigation);
    for (i = 0; i < FF_MAP_CELLS; ++i) {
        if (world->navigation[i] > farthest_distance &&
            world->tiles[i] == FF_TILE_FLOOR) {
            farthest_distance = world->navigation[i];
            farthest_index = i;
        }
    }
    if (farthest_index < 0) {
        farthest_index = ff_index(rooms[room_count - 1].center_x,
                                  rooms[room_count - 1].center_y);
    }
    world->tiles[farthest_index] = FF_TILE_EXIT;
    world->exit_x = (float)(farthest_index % FF_MAP_SIZE) + 0.5f;
    world->exit_y = (float)(farthest_index / FF_MAP_SIZE) + 0.5f;

    ff_spawn_entities(world);
}

bool ff_world_circle_collides(const FFWorld *world, float x, float y, float radius)
{
    int minimum_x = (int)floorf(x - radius);
    int maximum_x = (int)floorf(x + radius);
    int minimum_y = (int)floorf(y - radius);
    int maximum_y = (int)floorf(y + radius);
    int tile_y;

    for (tile_y = minimum_y; tile_y <= maximum_y; ++tile_y) {
        int tile_x;
        for (tile_x = minimum_x; tile_x <= maximum_x; ++tile_x) {
            float nearest_x;
            float nearest_y;
            float dx;
            float dy;
            if (!ff_world_is_solid(world, tile_x, tile_y)) {
                continue;
            }
            nearest_x = fmaxf((float)tile_x, fminf(x, (float)tile_x + 1.0f));
            nearest_y = fmaxf((float)tile_y, fminf(y, (float)tile_y + 1.0f));
            dx = x - nearest_x;
            dy = y - nearest_y;
            if (dx * dx + dy * dy < radius * radius) {
                return true;
            }
        }
    }
    return false;
}

void ff_world_move_circle(const FFWorld *world, float *x, float *y,
                          float dx, float dy, float radius)
{
    float candidate_x = *x + dx;
    float candidate_y = *y + dy;
    if (!ff_world_circle_collides(world, candidate_x, *y, radius)) {
        *x = candidate_x;
    }
    if (!ff_world_circle_collides(world, *x, candidate_y, radius)) {
        *y = candidate_y;
    }
}

bool ff_world_has_line_of_sight(const FFWorld *world,
                                float from_x, float from_y,
                                float to_x, float to_y)
{
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float distance = sqrtf(dx * dx + dy * dy);
    int steps;
    int i;

    if (distance < 0.001f) {
        return true;
    }
    steps = (int)(distance / 0.06f) + 1;
    for (i = 1; i < steps; ++i) {
        float amount = (float)i / (float)steps;
        int x = (int)floorf(from_x + dx * amount);
        int y = (int)floorf(from_y + dy * amount);
        if (ff_world_is_solid(world, x, y)) {
            return false;
        }
    }
    return true;
}

void ff_world_refresh_navigation(FFWorld *world, float target_x, float target_y)
{
    ff_compute_distances(world, (int)floorf(target_x), (int)floorf(target_y),
                         false, world->navigation);
}

static void ff_enemy_choose_direction(const FFWorld *world, const FFEnemy *enemy,
                                      float *out_x, float *out_y)
{
    int cell_x = (int)floorf(enemy->x);
    int cell_y = (int)floorf(enemy->y);
    int current_distance = -1;
    int best_x = cell_x;
    int best_y = cell_y;
    static const int directions[4][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    int direction;

    if (ff_world_in_bounds(cell_x, cell_y)) {
        current_distance = world->navigation[ff_index(cell_x, cell_y)];
    }
    for (direction = 0; direction < 4; ++direction) {
        int nx = cell_x + directions[direction][0];
        int ny = cell_y + directions[direction][1];
        int distance;
        if (!ff_world_is_walkable(world, nx, ny, false)) {
            continue;
        }
        distance = world->navigation[ff_index(nx, ny)];
        if (distance >= 0 && (current_distance < 0 || distance < current_distance)) {
            current_distance = distance;
            best_x = nx;
            best_y = ny;
        }
    }

    *out_x = (float)best_x + 0.5f - enemy->x;
    *out_y = (float)best_y + 0.5f - enemy->y;
}

int ff_world_update_enemies(FFWorld *world, float player_x, float player_y,
                            float delta_seconds)
{
    int total_damage = 0;
    int i;
    ff_world_refresh_navigation(world, player_x, player_y);

    for (i = 0; i < world->enemy_count; ++i) {
        FFEnemy *enemy = &world->enemies[i];
        float to_player_x;
        float to_player_y;
        float player_distance;

        if (!enemy->active) {
            continue;
        }
        enemy->anim_time += delta_seconds;
        if (enemy->attack_cooldown > 0.0f) {
            enemy->attack_cooldown -= delta_seconds;
        }
        if (enemy->state_time > 0.0f) {
            enemy->state_time -= delta_seconds;
        }

        if (enemy->state == FF_ENEMY_CAPTURED) {
            if (enemy->state_time <= 0.0f) {
                enemy->active = false;
            }
            continue;
        }
        if (enemy->state == FF_ENEMY_HURT && enemy->state_time > 0.0f) {
            continue;
        }

        to_player_x = player_x - enemy->x;
        to_player_y = player_y - enemy->y;
        player_distance = sqrtf(to_player_x * to_player_x + to_player_y * to_player_y);

        if (player_distance < 0.72f) {
            enemy->state = FF_ENEMY_ATTACK;
            if (enemy->attack_cooldown <= 0.0f) {
                int damage = 8 + world->floor_number - 1;
                if (damage > 25) {
                    damage = 25;
                }
                total_damage += damage;
                enemy->attack_cooldown = 0.9f;
                enemy->state_time = 0.25f;
            }
        } else {
            float move_x;
            float move_y;
            float length;
            if (player_distance < 12.0f ||
                ff_world_has_line_of_sight(world, enemy->x, enemy->y,
                                           player_x, player_y)) {
                ff_enemy_choose_direction(world, enemy, &move_x, &move_y);
                enemy->state = FF_ENEMY_WALK;
            } else {
                enemy->wander_angle += 0.37f * delta_seconds;
                move_x = cosf(enemy->wander_angle);
                move_y = sinf(enemy->wander_angle);
                enemy->state = FF_ENEMY_IDLE;
            }
            length = sqrtf(move_x * move_x + move_y * move_y);
            if (length > 0.001f) {
                float amount = enemy->speed * delta_seconds / length;
                ff_world_move_circle(world, &enemy->x, &enemy->y,
                                     move_x * amount, move_y * amount, 0.22f);
            }
        }
    }

    return total_damage;
}

int ff_world_interact(FFWorld *world, float player_x, float player_y,
                      float facing_angle)
{
    float direction_x = cosf(facing_angle);
    float direction_y = sinf(facing_angle);
    float distance;

    for (distance = 0.25f; distance <= 1.6f; distance += 0.12f) {
        int x = (int)floorf(player_x + direction_x * distance);
        int y = (int)floorf(player_y + direction_y * distance);
        uint8_t tile;
        if (!ff_world_in_bounds(x, y)) {
            break;
        }
        tile = world->tiles[ff_index(x, y)];
        if (tile == FF_TILE_DOOR) {
            world->tiles[ff_index(x, y)] = FF_TILE_FLOOR;
            return FF_INTERACT_DOOR;
        }
        if (tile >= FF_TILE_WALL_PLASTER && tile <= FF_TILE_WALL_RITUAL) {
            break;
        }
    }

    {
        float dx = world->exit_x - player_x;
        float dy = world->exit_y - player_y;
        float distance_squared = dx * dx + dy * dy;
        if (distance_squared <= 2.25f) {
            float length = sqrtf(distance_squared);
            float facing = length > 0.001f
                ? (dx * direction_x + dy * direction_y) / length
                : 1.0f;
            if (facing > 0.25f) {
                return FF_INTERACT_EXIT;
            }
        }
    }
    return FF_INTERACT_NONE;
}

int ff_world_reachable_count(const FFWorld *world, bool doors_passable)
{
    int16_t distances[FF_MAP_CELLS];
    int count = 0;
    int i;
    ff_compute_distances(world, (int)floorf(world->start_x),
                         (int)floorf(world->start_y), doors_passable, distances);
    for (i = 0; i < FF_MAP_CELLS; ++i) {
        if (distances[i] >= 0) {
            ++count;
        }
    }
    return count;
}

int ff_world_active_enemy_count(const FFWorld *world)
{
    int count = 0;
    int i;
    for (i = 0; i < world->enemy_count; ++i) {
        if (world->enemies[i].active) {
            ++count;
        }
    }
    return count;
}
