TARGET = sossy_x_huttl
.DEFAULT_GOAL := all
OBJS = main.o game.o world.o album.o tag_editor.o leaderboard.o renderer.o \
	audio.o loading_image.o title_image.o sprite_assets.o soundtrack_data.o

CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=gnu99
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspgu -lpspdisplay -lpspge -lpspctrl -lpsppower -lpsprtc \
	-lpspaudio -lpspmp3 -lpsputility -lm

BUILD_PRX = 1
PSP_FW_VERSION = 660
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Sossy X Huttl
PSP_EBOOT_PIC1 = assets/loading_metro_horse_psp.png

soundtrack_data.o: assets/soundtrack_psp.mp3
	psp-objcopy -I binary -O elf32-littlemips -B mips:allegrex $< $@

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: test
test:
	$(MAKE) -C tests run
