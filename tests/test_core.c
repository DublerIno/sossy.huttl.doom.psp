#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "album.h"
#include "game.h"
#include "leaderboard.h"
#include "tag_editor.h"
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

static void test_results_gate(void)
{
    FFGame game;
    FFInput input;
    memset(&game, 0, sizeof(game));
    memset(&input, 0, sizeof(input));
    game.mode = FF_MODE_RESULTS;
    game.score = 1234;
    input.confirm_pressed = true;
    ff_game_update(&game, &input, 0.016f);
    CHECK(game.mode == FF_MODE_RESULTS && game.score == 1234,
          "results remain visible until the application opens tag ranking");
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

static void test_tag_editor(void)
{
    FFTagEditor editor;
    FFTagInput input;
    FFTagBitmap packed;
    uint8_t unpacked[FF_TAG_PIXEL_COUNT];
    int center_x = FF_TAG_WIDTH / 2;
    int center_y = FF_TAG_HEIGHT / 2;
    int frame;

    ff_tag_editor_reset(&editor);
    CHECK(ff_tag_editor_is_blank(&editor), "new tag canvas starts blank");

    memset(&input, 0, sizeof(input));
    input.draw_held = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(editor.pixels[center_y * FF_TAG_WIDTH + center_x] == FF_TAG_BLACK,
          "X draws black ink at the cursor");

    memset(&input, 0, sizeof(input));
    ff_tag_editor_update(&editor, &input, 0.016f);
    input.color_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    memset(&input, 0, sizeof(input));
    input.dpad_x = 1;
    input.draw_held = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(editor.pixels[center_y * FF_TAG_WIDTH + center_x + 1] == FF_TAG_RED,
          "Square switches drawing to signal red");

    memset(&input, 0, sizeof(input));
    ff_tag_editor_update(&editor, &input, 0.016f);
    input.undo_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(editor.pixels[center_y * FF_TAG_WIDTH + center_x] == FF_TAG_BLACK &&
          editor.pixels[center_y * FF_TAG_WIDTH + center_x + 1] == FF_TAG_EMPTY,
          "Triangle restores the canvas before the previous stroke");

    memset(&input, 0, sizeof(input));
    input.draw_held = true;
    input.dpad_x = 2;
    ff_tag_editor_update(&editor, &input, 0.016f);
    memset(&input, 0, sizeof(input));
    ff_tag_editor_update(&editor, &input, 0.016f);
    input.clear_held = true;
    for (frame = 0; frame < 8; ++frame) {
        ff_tag_editor_update(&editor, &input, 0.1f);
    }
    CHECK(ff_tag_editor_is_blank(&editor), "holding L and R clears the canvas");
    memset(&input, 0, sizeof(input));
    input.undo_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(!ff_tag_editor_is_blank(&editor), "a cleared canvas can be undone");

    ff_tag_pack(editor.pixels, &packed);
    memset(unpacked, 0, sizeof(unpacked));
    ff_tag_unpack(&packed, unpacked);
    CHECK(memcmp(unpacked, editor.pixels, sizeof(unpacked)) == 0,
          "two-bit tag packing preserves every canvas pixel");
    CHECK(ff_tag_bitmap_pixel(&packed, center_x, center_y) == FF_TAG_BLACK,
          "packed tag pixels can be sampled for leaderboard rendering");

    ff_tag_editor_reset(&editor);
    editor.cursor_x = 2.0f;
    editor.cursor_y = 2.0f;
    memset(&input, 0, sizeof(input));
    input.analog_x = 1.0f;
    input.draw_held = true;
    ff_tag_editor_update(&editor, &input, 0.1f);
    CHECK(editor.pixels[2 * FF_TAG_WIDTH + 2] == FF_TAG_BLACK &&
          editor.pixels[2 * FF_TAG_WIDTH + 3] == FF_TAG_BLACK &&
          editor.pixels[2 * FF_TAG_WIDTH + 4] == FF_TAG_BLACK,
          "analog cursor movement interpolates a continuous stroke");
    memset(&input, 0, sizeof(input));
    ff_tag_editor_update(&editor, &input, 0.016f);
    input.erase_held = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(editor.pixels[2 * FF_TAG_WIDTH + 4] == FF_TAG_EMPTY,
          "Circle erases the pixel under the cursor");
    memset(&input, 0, sizeof(input));
    input.analog_x = 1.0f;
    input.analog_y = -1.0f;
    for (frame = 0; frame < 100; ++frame) {
        ff_tag_editor_update(&editor, &input, 0.1f);
    }
    CHECK(editor.cursor_x == (float)(FF_TAG_WIDTH - 1) &&
          editor.cursor_y == 0.0f,
          "tag cursor remains clamped to the canvas");

    ff_tag_editor_reset(&editor);
    memset(&input, 0, sizeof(input));
    input.finish_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(!editor.confirming && editor.message_seconds > 0.0f,
          "blank tags cannot open the save confirmation");
    memset(&input, 0, sizeof(input));
    input.draw_held = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    memset(&input, 0, sizeof(input));
    input.finish_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    CHECK(editor.confirming, "non-empty tags open the save confirmation");
    memset(&input, 0, sizeof(input));
    input.cancel_pressed = true;
    CHECK(!ff_tag_editor_update(&editor, &input, 0.016f) && !editor.confirming,
          "Circle returns from confirmation to editing");
    memset(&input, 0, sizeof(input));
    input.finish_pressed = true;
    ff_tag_editor_update(&editor, &input, 0.016f);
    memset(&input, 0, sizeof(input));
    input.confirm_pressed = true;
    CHECK(ff_tag_editor_update(&editor, &input, 0.016f),
          "X confirms and submits a finished tag");
}

static FFTagBitmap make_test_tag(int value)
{
    FFTagBitmap tag;
    memset(&tag, 0, sizeof(tag));
    tag.data[0] = (uint8_t)((value & 1) ? FF_TAG_RED : FF_TAG_BLACK);
    return tag;
}

static void test_leaderboard(void)
{
    FFLeaderboard leaderboard;
    FFLeaderboard loaded;
    FFTagBitmap tag;
    uint8_t serialized[FF_LEADERBOARD_FILE_MAX_BYTES];
    size_t serialized_size;
    uint32_t replaced_sequence;
    char path[128];
    int score;

    ff_leaderboard_reset(&leaderboard);
    CHECK(leaderboard.count == 0 && ff_leaderboard_page_count(&leaderboard) == 1,
          "empty leaderboard still exposes one display page");
    for (score = 0; score < 25; ++score) {
        tag = make_test_tag(score);
        ff_leaderboard_insert(&leaderboard, score * 10, &tag);
    }
    CHECK(leaderboard.count == FF_LEADERBOARD_CAPACITY,
          "leaderboard retains exactly the top twenty runs");
    CHECK(leaderboard.entries[0].score == 240 &&
          leaderboard.entries[19].score == 50,
          "leaderboard evicts the five lowest scores");
    CHECK(ff_leaderboard_page_count(&leaderboard) == 4,
          "twenty entries produce four five-entry pages");
    CHECK(!ff_leaderboard_qualifies(&leaderboard, 49),
          "a score below twentieth place does not qualify");
    CHECK(ff_leaderboard_qualifies(&leaderboard, 50),
          "a score tied with twentieth place qualifies");
    replaced_sequence = leaderboard.entries[19].sequence;
    tag = make_test_tag(99);
    CHECK(ff_leaderboard_insert(&leaderboard, 50, &tag) == 19,
          "a newest equal score replaces the older tied entry");
    CHECK(leaderboard.entries[19].sequence > replaced_sequence,
          "equal scores use newest-first ordering");
    CHECK(ff_leaderboard_insert(&leaderboard, 49, &tag) == -1,
          "non-qualifying insertion leaves the table unchanged");

    serialized_size = ff_leaderboard_serialize(
        &leaderboard, serialized, sizeof(serialized));
    CHECK(serialized_size > 0 &&
          ff_leaderboard_deserialize(&loaded, serialized, serialized_size),
          "versioned leaderboard data survives a serialization round trip");
    CHECK(loaded.count == leaderboard.count &&
          loaded.entries[0].score == leaderboard.entries[0].score &&
          memcmp(loaded.entries[19].tag.data,
                 leaderboard.entries[19].tag.data,
                 FF_TAG_PACKED_BYTES) == 0,
          "round-tripped scores and tags match the source table");
    CHECK(!ff_leaderboard_deserialize(&loaded, serialized,
                                      serialized_size - 1),
          "truncated leaderboard data is rejected");
    serialized[0] ^= 0x01u;
    CHECK(!ff_leaderboard_deserialize(&loaded, serialized, serialized_size),
          "unknown leaderboard magic is rejected");
    serialized[0] ^= 0x01u;
    serialized[8] = 2u;
    CHECK(!ff_leaderboard_deserialize(&loaded, serialized, serialized_size),
          "unknown leaderboard versions are rejected");
    serialized[8] = 1u;
    serialized[20] ^= 0x40u;
    CHECK(!ff_leaderboard_deserialize(&loaded, serialized, serialized_size),
          "CRC validation rejects damaged leaderboard data");

    CHECK(ff_leaderboard_make_path(path, sizeof(path),
              "ms0:/PSP/GAME/SOSSYXHUTTL/EBOOT.PBP") &&
          strcmp(path, "ms0:/PSP/GAME/SOSSYXHUTTL/SCORES.DAT") == 0,
          "save path follows the EBOOT directory on PSP storage");
    CHECK(ff_leaderboard_make_path(path, sizeof(path), "EBOOT.PBP") &&
          strcmp(path, "SCORES.DAT") == 0,
          "save path has a safe relative fallback");

    strcpy(path, "test_scores.dat");
    remove(path);
    remove("test_scores.dat.TMP");
    remove("test_scores.dat.BAK");
    CHECK(ff_leaderboard_load(&loaded, path) ==
              FF_LEADERBOARD_LOAD_NOT_FOUND,
          "a missing score file is treated as an empty first launch");
    CHECK(ff_leaderboard_save(&leaderboard, path),
          "leaderboard writes an atomic score file");
    ff_leaderboard_reset(&loaded);
    CHECK(ff_leaderboard_load(&loaded, path) == FF_LEADERBOARD_LOAD_OK &&
          loaded.count == leaderboard.count,
          "saved scores load from disk with their full entry count");
    remove(path);
    remove("test_scores.dat.TMP");
    remove("test_scores.dat.BAK");
}

int main(void)
{
    test_generation();
    test_photography();
    test_collision_and_interaction();
    test_results_gate();
    test_long_run();
    test_album();
    test_tag_editor();
    test_leaderboard();

    if (failures != 0) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("All %d Sossy X Huttl core checks passed.\n", checks);
    return EXIT_SUCCESS;
}
