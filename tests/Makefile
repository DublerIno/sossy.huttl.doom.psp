CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c99

test_core: test_core.c ../game.c ../world.c ../album.c ../game.h ../world.h ../album.h
	$(CC) $(CFLAGS) -I.. test_core.c ../game.c ../world.c ../album.c -lm -o test_core

run: test_core
	./test_core

clean:
	$(RM) test_core

.PHONY: run clean
