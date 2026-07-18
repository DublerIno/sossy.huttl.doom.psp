#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "album.h"
#include "game.h"
#include "world.h"

static int failures = 0;
static int checks = 0;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
    } \
} while (0)

static int tile_index(int x, int y)
{
    return y * FF_MAP_SIZE + x;
}

static void test_generation(void)
{
    uint32_t seed;
    for (seed = 1; seed <= 2000; ++seed) {
        FFWorld world;
        int floor_number = 1 + (int)(seed % 40u);
        int expected_enemies = 4 + 2 * (floor_number - 1);
        int i;
        if (expected_enemies > FF_MAX_ENEMIES) {
            expected_enemies = FF_MAX_ENEMIES;
        }

        ff_world_generate(&world, seed * 2654435761u, floor_number);
        CHECK(ff_world_reachable_count(&world, true) >= 40,
              "generated floor has a useful connected area");
        CHECK(world.tiles[tile_index((int)world.exit_x, (int)world.exit_y)] == FF_TILE_EXIT,
              "generated floor has an exit tile");
        CHECK(world.enemy_count == expected_enemies,
              "difficulty curve produces expected enemy count");
        CHECK(!ff_world_circle_collides(&world, world.start_x, world.start_y, 0.22f),
              "player start is not embedded in a wall");
        CHECK(!ff_world_circle_collides(&world, world.exit_x, world.exit_y, 0.15f),
              "exit remains walkable");

        for (i = 0; i < world.enemy_count; ++i) {
            CHECK(world.enemies[i].active, "spawned enemy is active");
            CHECK(!ff_world_circle_collides(&world, world.enemies[i].x,
                                            world.enemies[i].y, 0.15f),
                  "enemy does not spawn inside a wall");
        }
        for (i = 0; i < world.pickup_count; ++i) {
            CHECK(world.pickups[i].active, "spawned pickup is active");
            CHECK(!ff_world_circle_collides(&world, world.pickups[i].x,
                                            world.pickups[i].y, 0.10f),
                  "pickup does not spawn inside a wall");
        }
    }
}

static void make_photo_test_game(FFGame *game)
{
    int y;
    memset(game, 0, sizeof(*game));
    game->mode = FF_MODE_PLAYING;
    game->health = FF_MAX_HEALTH;
    game->film = 5;
    game->player_x = 4.5f;
    game->player_y = 4.5f;
    game->player_angle = 0.0f;
    game->world.enemy_count = 1;
    for (y = 0; y < FF_MAP_SIZE; ++y) {
        int x;
        for (x = 0; x < FF_MAP_SIZE; ++x) {
            game->world.tiles[tile_index(x, y)] =
                (x == 0 || y == 0 || x == FF_MAP_SIZE - 1 || y == FF_MAP_SIZE - 1)
                ? FF_TILE_WALL_PLASTER : FF_TILE_FLOOR;
        }
    }
    game->world.enemies[0].x = 7.5f;
    game->world.enemies[0].y = 4.5f;
    game->world.enemies[0].health = 100.0f;
    game->world.enemies[0].active = true;
    game->world.enemies[0].state = FF_ENEMY_IDLE;
}

static void test_photography(void)
{
    FFGame game;
    FFPhotoResult result;

    make_photo_test_game(&game);
    result = ff_game_take_photo(&game);
    CHECK(result.hit_count == 1, "centered visible enemy is photographed");
    CHECK(result.damage_dealt >= 80, "close centered photo deals strong exposure");
    CHECK(game.film == 4, "successful shutter consumes one film frame");
    CHECK(game.flash_cooldown > 0.0f, "shutter starts flash cooldown");

    make_photo_test_game(&game);
    game.world.tiles[tile_index(6, 4)] = FF_TILE_WALL_BRICK;
    result = ff_game_take_photo(&game);
    CHECK(result.hit_count == 0, "wall blocks photographic exposure");
    CHECK(game.film == 4, "missed shutter still consumes film");

    make_photo_test_game(&game);
    game.player_angle = 1.0f;
    result = ff_game_take_photo(&game);
    CHECK(result.hit_count == 0, "enemy outside viewfinder is not damaged");

    make_photo_test_game(&game);
    game.film = 0;
    result = ff_game_take_photo(&game);
    CHECK((result.event_flags & FF_EVENT_EMPTY) != 0,
          "empty camera reports empty event");
    CHECK(game.photographs_taken == 0, "empty shutter is not counted as a photograph");

    make_photo_test_game(&game);
    game.world.enemies[0].health = 20.0f;
    result = ff_game_take_photo(&game);
    CHECK(result.capture_count == 1, "exposure removes enemy when health reaches zero");
    CHECK(game.world.enemies[0].state == FF_ENEMY_CAPTURED,
          "captured enemy enters disappearance state");
    CHECK(result.photo_score >= 750, "capture awards score bonus");
}

static void test_collision_and_interaction(void)
{
    FFWorld world;
    float x = 3.5f;
    float y = 3.5f;
    int row;
    memset(&world, 0, sizeof(world));
    for (row = 0; row < FF_MAP_SIZE; ++row) {
        int column;
        for (column = 0; column < FF_MAP_SIZE; ++column) {
            world.tiles[tile_index(column, row)] =
                (column == 0 || row == 0 || column == FF_MAP_SIZE - 1 ||
                 row == FF_MAP_SIZE - 1) ? FF_TILE_WALL_PLASTER : FF_TILE_FLOOR;
        }
    }
    world.tiles[tile_index(5, 3)] = FF_TILE_DOOR;
    ff_world_move_circle(&world, &x, &y, 2.0f, 0.0f, 0.22f);
    CHECK(x < 4.8f, "closed door blocks player movement");
    CHECK(ff_world_interact(&world, x, y, 0.0f) == FF_INTERACT_DOOR,
          "interaction opens facing door");
    CHECK(world.tiles[tile_index(5, 3)] == FF_TILE_FLOOR,
          "opened door becomes walkable floor");
}

static void test_long_run(void)
{
    FFGame game;
    int floor_number;
    ff_game_start_run(&game, 0x12345678u);
    for (floor_number = 1; floor_number <= 100; ++floor_number) {
        CHECK(game.world.enemy_count <= FF_MAX_ENEMIES,
              "long run respects fixed enemy capacity");
        CHECK(game.world.pickup_count <= FF_MAX_PICKUPS,
              "long run respects fixed pickup capacity");
        game.floor_number = floor_number + 1;
        ff_world_generate(&game.world, 0x12345678u ^ (uint32_t)floor_number,
                          game.floor_number);
    }
    CHECK(sizeof(FFGame) < 65536u,
          "portable game state remains compact and floor-independent");
}

static void test_album(void)
{
    FFAlbum album;
    uint16_t source[4] = {0xf800u, 0x07e0u, 0x001fu, 0xffffu};
    int photo;
    ff_album_reset(&album);
    for (photo = 0; photo < 10; ++photo) {
        ff_album_consider(&album, source, 2, 2, 2, photo * 10,
                          photo + 1, photo & 3, photo & 1);
    }
    CHECK(album.count == FF_ALBUM_CAPACITY,
          "album retains only its fixed thumbnail capacity");
    CHECK(ff_album_ranked(&album, 0)->score == 90,
          "contact sheet ranks the strongest photograph first");
    CHECK(ff_album_ranked(&album, 7)->score == 20,
          "album evicts its lowest-scoring photographs");
    CHECK(ff_album_ranked(&album, -1) == NULL,
          "negative album rank is rejected");
    CHECK(ff_album_ranked(&album, FF_ALBUM_CAPACITY) == NULL,
          "album rank beyond stored count is rejected");
    CHECK(ff_album_ranked(&album, 0)->pixels[0] == 0x7befu,
          "thumbnail downsampling averages RGB565 source texels");
}

int main(void)
{
    test_generation();
    test_photography();
    test_collision_and_interaction();
    test_long_run();
    test_album();

    if (failures != 0) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("All %d Sossy X Huttl core checks passed.\n", checks);
    return EXIT_SUCCESS;
}
