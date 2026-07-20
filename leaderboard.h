#ifndef SOSSY_X_HUTTL_LEADERBOARD_H
#define SOSSY_X_HUTTL_LEADERBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tag_editor.h"

#define FF_LEADERBOARD_CAPACITY 20
#define FF_LEADERBOARD_PAGE_SIZE 5
#define FF_LEADERBOARD_PATH_CAPACITY 512
#define FF_LEADERBOARD_FILE_MAX_BYTES \
    (16 + FF_LEADERBOARD_CAPACITY * (8 + FF_TAG_PACKED_BYTES) + 4)

typedef struct {
    int score;
    uint32_t sequence;
    FFTagBitmap tag;
} FFLeaderboardEntry;

typedef struct {
    FFLeaderboardEntry entries[FF_LEADERBOARD_CAPACITY];
    int count;
    uint32_t next_sequence;
} FFLeaderboard;

typedef enum {
    FF_LEADERBOARD_LOAD_OK = 0,
    FF_LEADERBOARD_LOAD_NOT_FOUND,
    FF_LEADERBOARD_LOAD_INVALID,
    FF_LEADERBOARD_LOAD_IO_ERROR
} FFLeaderboardLoadStatus;

void ff_leaderboard_reset(FFLeaderboard *leaderboard);
bool ff_leaderboard_qualifies(const FFLeaderboard *leaderboard, int score);
int ff_leaderboard_insert(FFLeaderboard *leaderboard, int score,
                          const FFTagBitmap *tag);
int ff_leaderboard_page_count(const FFLeaderboard *leaderboard);
size_t ff_leaderboard_serialize(const FFLeaderboard *leaderboard,
                                uint8_t *destination,
                                size_t destination_size);
bool ff_leaderboard_deserialize(FFLeaderboard *leaderboard,
                                const uint8_t *source, size_t source_size);
bool ff_leaderboard_make_path(char *destination, size_t destination_size,
                              const char *executable_path);
FFLeaderboardLoadStatus ff_leaderboard_load(FFLeaderboard *leaderboard,
                                             const char *path);
bool ff_leaderboard_save(const FFLeaderboard *leaderboard, const char *path);

#endif
