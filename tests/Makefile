CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99

test_core: test_core.c ../game.c ../world.c ../album.c ../tag_editor.c \
	../leaderboard.c ../game.h ../world.h ../album.h ../tag_editor.h \
	../leaderboard.h
	$(CC) $(CFLAGS) -I.. test_core.c ../game.c ../world.c ../album.c \
		../tag_editor.c ../leaderboard.c -lm -o test_core

run: test_core
	./test_core

clean:
	$(RM) test_core

.PHONY: run clean
