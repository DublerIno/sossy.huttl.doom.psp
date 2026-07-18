TARGET = sossy_x_huttl
OBJS = main.o game.o world.o album.o renderer.o audio.o loading_image.o title_image.o

CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspgu -lpspdisplay -lpspge -lpspctrl -lpsppower -lpsprtc -lpspaudio -lm

BUILD_PRX = 1
PSP_FW_VERSION = 660
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Sossy X Huttl
PSP_EBOOT_PIC1 = assets/loading_metro_horse_psp.png

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: test
test:
	$(MAKE) -C tests run
