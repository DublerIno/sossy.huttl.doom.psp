#ifndef FLASHFRAME_ALBUM_H
#define FLASHFRAME_ALBUM_H

#include <stdint.h>

#define FF_ALBUM_CAPACITY 8
#define FF_THUMB_WIDTH 120
#define FF_THUMB_HEIGHT 68

typedef struct {
    uint16_t pixels[FF_THUMB_WIDTH * FF_THUMB_HEIGHT];
    int score;
    int floor_number;
    int hit_count;
    int capture_count;
    int sequence;
} FFStoredPhoto;

typedef struct {
    FFStoredPhoto photos[FF_ALBUM_CAPACITY];
    int count;
    int next_sequence;
} FFAlbum;

void ff_album_reset(FFAlbum *album);
void ff_album_consider(FFAlbum *album, const uint16_t *source,
                       int source_width, int source_height, int source_stride,
                       int score, int floor_number, int hit_count,
                       int capture_count);
const FFStoredPhoto *ff_album_ranked(const FFAlbum *album, int rank);

#endif
