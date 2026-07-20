#include "leaderboard.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define FF_LEADERBOARD_VERSION 1u
#define FF_LEADERBOARD_HEADER_BYTES 16u
#define FF_LEADERBOARD_RECORD_BYTES (8u + FF_TAG_PACKED_BYTES)
#define FF_LEADERBOARD_CRC_BYTES 4u

static const uint8_t FF_LEADERBOARD_MAGIC[8] = {
    'S', 'X', 'H', 'T', 'A', 'G', '0', '1'
};

static void ff_write_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xffu);
    destination[1] = (uint8_t)(value >> 8);
}

static void ff_write_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xffu);
    destination[1] = (uint8_t)((value >> 8) & 0xffu);
    destination[2] = (uint8_t)((value >> 16) & 0xffu);
    destination[3] = (uint8_t)(value >> 24);
}

static uint16_t ff_read_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8));
}

static uint32_t ff_read_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint32_t ff_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    size_t byte;
    for (byte = 0; byte < size; ++byte) {
        int bit;
        crc ^= data[byte];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static bool ff_tag_bitmap_valid(const FFTagBitmap *tag)
{
    int pixel;
    for (pixel = 0; pixel < FF_TAG_PIXEL_COUNT; ++pixel) {
        unsigned int value = (tag->data[pixel >> 2] >>
                              ((pixel & 3) * 2)) & 3u;
        if (value > FF_TAG_RED) return false;
    }
    return true;
}

static void ff_leaderboard_normalize_sequences(FFLeaderboard *leaderboard)
{
    int entry;
    for (entry = 0; entry < leaderboard->count; ++entry) {
        leaderboard->entries[entry].sequence =
            (uint32_t)(leaderboard->count - entry);
    }
    leaderboard->next_sequence = (uint32_t)leaderboard->count + 1u;
}

void ff_leaderboard_reset(FFLeaderboard *leaderboard)
{
    memset(leaderboard, 0, sizeof(*leaderboard));
    leaderboard->next_sequence = 1u;
}

bool ff_leaderboard_qualifies(const FFLeaderboard *leaderboard, int score)
{
    if (score < 0) score = 0;
    return leaderboard->count < FF_LEADERBOARD_CAPACITY ||
           score >= leaderboard->entries[leaderboard->count - 1].score;
}

int ff_leaderboard_insert(FFLeaderboard *leaderboard, int score,
                          const FFTagBitmap *tag)
{
    FFLeaderboardEntry new_entry;
    int insertion;
    int last;
    if (tag == NULL || !ff_tag_bitmap_valid(tag) ||
        !ff_leaderboard_qualifies(leaderboard, score)) return -1;
    if (score < 0) score = 0;
    if (leaderboard->next_sequence == 0u ||
        leaderboard->next_sequence == UINT32_MAX) {
        ff_leaderboard_normalize_sequences(leaderboard);
    }
    new_entry.score = score;
    new_entry.sequence = leaderboard->next_sequence++;
    new_entry.tag = *tag;

    insertion = 0;
    while (insertion < leaderboard->count &&
           leaderboard->entries[insertion].score > score) {
        ++insertion;
    }
    last = leaderboard->count < FF_LEADERBOARD_CAPACITY
        ? leaderboard->count : FF_LEADERBOARD_CAPACITY - 1;
    while (last > insertion) {
        leaderboard->entries[last] = leaderboard->entries[last - 1];
        --last;
    }
    leaderboard->entries[insertion] = new_entry;
    if (leaderboard->count < FF_LEADERBOARD_CAPACITY) ++leaderboard->count;
    return insertion;
}

int ff_leaderboard_page_count(const FFLeaderboard *leaderboard)
{
    int pages = (leaderboard->count + FF_LEADERBOARD_PAGE_SIZE - 1) /
                FF_LEADERBOARD_PAGE_SIZE;
    return pages > 0 ? pages : 1;
}

size_t ff_leaderboard_serialize(const FFLeaderboard *leaderboard,
                                uint8_t *destination,
                                size_t destination_size)
{
    size_t required;
    size_t offset;
    int entry;
    if (leaderboard->count < 0 ||
        leaderboard->count > FF_LEADERBOARD_CAPACITY) return 0;
    required = FF_LEADERBOARD_HEADER_BYTES +
               (size_t)leaderboard->count * FF_LEADERBOARD_RECORD_BYTES +
               FF_LEADERBOARD_CRC_BYTES;
    if (destination == NULL || destination_size < required) return 0;

    memcpy(destination, FF_LEADERBOARD_MAGIC, sizeof(FF_LEADERBOARD_MAGIC));
    ff_write_u16(destination + 8, FF_LEADERBOARD_VERSION);
    ff_write_u16(destination + 10, (uint16_t)leaderboard->count);
    ff_write_u32(destination + 12, leaderboard->next_sequence);
    offset = FF_LEADERBOARD_HEADER_BYTES;
    for (entry = 0; entry < leaderboard->count; ++entry) {
        const FFLeaderboardEntry *source = &leaderboard->entries[entry];
        ff_write_u32(destination + offset, (uint32_t)source->score);
        ff_write_u32(destination + offset + 4, source->sequence);
        memcpy(destination + offset + 8, source->tag.data,
               FF_TAG_PACKED_BYTES);
        offset += FF_LEADERBOARD_RECORD_BYTES;
    }
    ff_write_u32(destination + offset, ff_crc32(destination, offset));
    return required;
}

bool ff_leaderboard_deserialize(FFLeaderboard *leaderboard,
                                const uint8_t *source, size_t source_size)
{
    FFLeaderboard loaded;
    uint16_t count;
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t maximum_sequence = 0u;
    size_t expected_size;
    size_t offset;
    int entry;
    if (source == NULL || source_size < FF_LEADERBOARD_HEADER_BYTES +
                                             FF_LEADERBOARD_CRC_BYTES) {
        return false;
    }
    if (memcmp(source, FF_LEADERBOARD_MAGIC,
               sizeof(FF_LEADERBOARD_MAGIC)) != 0 ||
        ff_read_u16(source + 8) != FF_LEADERBOARD_VERSION) return false;
    count = ff_read_u16(source + 10);
    if (count > FF_LEADERBOARD_CAPACITY) return false;
    expected_size = FF_LEADERBOARD_HEADER_BYTES +
                    (size_t)count * FF_LEADERBOARD_RECORD_BYTES +
                    FF_LEADERBOARD_CRC_BYTES;
    if (source_size != expected_size) return false;
    expected_crc = ff_read_u32(source + source_size - FF_LEADERBOARD_CRC_BYTES);
    actual_crc = ff_crc32(source, source_size - FF_LEADERBOARD_CRC_BYTES);
    if (expected_crc != actual_crc) return false;

    ff_leaderboard_reset(&loaded);
    loaded.count = (int)count;
    loaded.next_sequence = ff_read_u32(source + 12);
    offset = FF_LEADERBOARD_HEADER_BYTES;
    for (entry = 0; entry < loaded.count; ++entry) {
        uint32_t stored_score = ff_read_u32(source + offset);
        FFLeaderboardEntry *destination = &loaded.entries[entry];
        if (stored_score > INT_MAX) return false;
        destination->score = (int)stored_score;
        destination->sequence = ff_read_u32(source + offset + 4);
        memcpy(destination->tag.data, source + offset + 8,
               FF_TAG_PACKED_BYTES);
        if (!ff_tag_bitmap_valid(&destination->tag)) return false;
        if (entry > 0 &&
            (loaded.entries[entry - 1].score < destination->score ||
             (loaded.entries[entry - 1].score == destination->score &&
              loaded.entries[entry - 1].sequence < destination->sequence))) {
            return false;
        }
        if (destination->sequence > maximum_sequence) {
            maximum_sequence = destination->sequence;
        }
        offset += FF_LEADERBOARD_RECORD_BYTES;
    }
    if (loaded.next_sequence == 0u ||
        loaded.next_sequence <= maximum_sequence) {
        ff_leaderboard_normalize_sequences(&loaded);
    }
    *leaderboard = loaded;
    return true;
}

bool ff_leaderboard_make_path(char *destination, size_t destination_size,
                              const char *executable_path)
{
    const char *separator;
    size_t directory_length;
    static const char filename[] = "SCORES.DAT";
    if (destination == NULL || destination_size == 0) return false;
    if (executable_path == NULL) executable_path = "";
    separator = strrchr(executable_path, '/');
    if (separator == NULL) separator = strrchr(executable_path, '\\');
    directory_length = separator == NULL ? 0u :
                       (size_t)(separator - executable_path + 1);
    if (directory_length + sizeof(filename) > destination_size) {
        destination[0] = '\0';
        return false;
    }
    if (directory_length > 0) {
        memcpy(destination, executable_path, directory_length);
    }
    memcpy(destination + directory_length, filename, sizeof(filename));
    return true;
}

FFLeaderboardLoadStatus ff_leaderboard_load(FFLeaderboard *leaderboard,
                                             const char *path)
{
    uint8_t data[FF_LEADERBOARD_FILE_MAX_BYTES + 1];
    FILE *file;
    size_t size;
    int read_error;
    if (path == NULL) return FF_LEADERBOARD_LOAD_IO_ERROR;
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? FF_LEADERBOARD_LOAD_NOT_FOUND
                               : FF_LEADERBOARD_LOAD_IO_ERROR;
    }
    size = fread(data, 1, sizeof(data), file);
    read_error = ferror(file);
    if (fclose(file) != 0 || read_error) return FF_LEADERBOARD_LOAD_IO_ERROR;
    if (size > FF_LEADERBOARD_FILE_MAX_BYTES ||
        !ff_leaderboard_deserialize(leaderboard, data, size)) {
        return FF_LEADERBOARD_LOAD_INVALID;
    }
    return FF_LEADERBOARD_LOAD_OK;
}

bool ff_leaderboard_save(const FFLeaderboard *leaderboard, const char *path)
{
    uint8_t data[FF_LEADERBOARD_FILE_MAX_BYTES];
    char temporary_path[FF_LEADERBOARD_PATH_CAPACITY];
    char backup_path[FF_LEADERBOARD_PATH_CAPACITY];
    size_t size;
    size_t path_length;
    FILE *file;
    bool had_backup = false;
    bool write_ok;
    if (path == NULL) return false;
    path_length = strlen(path);
    if (path_length + 5 >= sizeof(temporary_path)) return false;
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length, ".TMP", 5);
    memcpy(backup_path, path, path_length);
    memcpy(backup_path + path_length, ".BAK", 5);

    size = ff_leaderboard_serialize(leaderboard, data, sizeof(data));
    if (size == 0) return false;
    file = fopen(temporary_path, "wb");
    if (file == NULL) return false;
    write_ok = fwrite(data, 1, size, file) == size;
    if (write_ok) write_ok = fflush(file) == 0;
    if (fclose(file) != 0) write_ok = false;
    if (!write_ok) {
        remove(temporary_path);
        return false;
    }

    remove(backup_path);
    if (rename(path, backup_path) == 0) {
        had_backup = true;
    } else if (errno != ENOENT) {
        remove(temporary_path);
        return false;
    }
    if (rename(temporary_path, path) != 0) {
        if (had_backup) rename(backup_path, path);
        remove(temporary_path);
        return false;
    }
    if (had_backup) remove(backup_path);
    return true;
}
