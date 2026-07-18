#include "album.h"

#include <stdbool.h>
#include <string.h>

static uint16_t ff_average_565(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    unsigned int red = (a & 31u) + (b & 31u) + (c & 31u) + (d & 31u);
    unsigned int green = ((a >> 5) & 63u) + ((b >> 5) & 63u) +
                         ((c >> 5) & 63u) + ((d >> 5) & 63u);
    unsigned int blue = ((a >> 11) & 31u) + ((b >> 11) & 31u) +
                        ((c >> 11) & 31u) + ((d >> 11) & 31u);
    return (uint16_t)(((blue / 4u) << 11) | ((green / 4u) << 5) | (red / 4u));
}

void ff_album_reset(FFAlbum *album)
{
    memset(album, 0, sizeof(*album));
}

void ff_album_consider(FFAlbum *album, const uint16_t *source,
                       int source_width, int source_height, int source_stride,
                       int score, int floor_number, int hit_count,
                       int capture_count)
{
    FFStoredPhoto *destination;
    int destination_index;
    int y;

    if (album->count < FF_ALBUM_CAPACITY) {
        destination_index = album->count++;
    } else {
        int lowest_score = album->photos[0].score;
        int i;
        destination_index = 0;
        for (i = 1; i < FF_ALBUM_CAPACITY; ++i) {
            if (album->photos[i].score < lowest_score ||
                (album->photos[i].score == lowest_score &&
                 album->photos[i].sequence < album->photos[destination_index].sequence)) {
                lowest_score = album->photos[i].score;
                destination_index = i;
            }
        }
        if (score < lowest_score) {
            return;
        }
    }

    destination = &album->photos[destination_index];
    destination->score = score;
    destination->floor_number = floor_number;
    destination->hit_count = hit_count;
    destination->capture_count = capture_count;
    destination->sequence = album->next_sequence++;

    for (y = 0; y < FF_THUMB_HEIGHT; ++y) {
        int source_y0 = y * source_height / FF_THUMB_HEIGHT;
        int source_y1 = (source_y0 + 1 < source_height) ? source_y0 + 1 : source_y0;
        int x;
        for (x = 0; x < FF_THUMB_WIDTH; ++x) {
            int source_x0 = x * source_width / FF_THUMB_WIDTH;
            int source_x1 = (source_x0 + 1 < source_width) ? source_x0 + 1 : source_x0;
            uint16_t a = source[source_y0 * source_stride + source_x0];
            uint16_t b = source[source_y0 * source_stride + source_x1];
            uint16_t c = source[source_y1 * source_stride + source_x0];
            uint16_t d = source[source_y1 * source_stride + source_x1];
            destination->pixels[y * FF_THUMB_WIDTH + x] = ff_average_565(a, b, c, d);
        }
    }
}

const FFStoredPhoto *ff_album_ranked(const FFAlbum *album, int rank)
{
    bool selected[FF_ALBUM_CAPACITY] = {false};
    int selected_index = -1;
    int selection;

    if (rank < 0 || rank >= album->count) {
        return NULL;
    }
    for (selection = 0; selection <= rank; ++selection) {
        int best = -1;
        int i;
        for (i = 0; i < album->count; ++i) {
            if (selected[i]) {
                continue;
            }
            if (best < 0 || album->photos[i].score > album->photos[best].score ||
                (album->photos[i].score == album->photos[best].score &&
                 album->photos[i].sequence > album->photos[best].sequence)) {
                best = i;
            }
        }
        selected[best] = true;
        selected_index = best;
    }
    return &album->photos[selected_index];
}
