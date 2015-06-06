PSPSDK=$(shell psp-config --pspsdk-path)
PSPBIN = $(PSPSDK)/../bin

TARGET = pspdc
OBJS = main.o psplog.o drone.o menu.o color.o ui.o

CFLAGS = -g -O2 -G0 -Wall -Wextra
CXXFLAGS = -g -O2 -Wall -Wextra -fno-exceptions -fno-rtti

LIBS := \
	-larcommands \
	-larnetwork \
	-larnetworkal \
	-lardiscovery \
	-larstream \
	-larsal \
#	-lpspgu \
#	-lpsputility \

PSP_FW_VERSION = 635
BUILD_PRX=1

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Drone Control

# SDL libs
# Don't use sdl-config --cflags because we want to define our main
# Don't use sdl-config --libs since it brokes imports fixup
CFLAGS += -D_GNU_SOURCE=1
LIBS += -lSDL_ttf \
	-lSDL \
	-lGL \
	-lm \
	-lpspgu \
	-lpsphprm \
	-lpsprtc \
	-lpspaudio \
	-lpspirkeyb \
	-lpsppower \
	-lpspvfpu

# Freetype
CFLAGS += $(shell $(PSPBIN)/freetype-config --cflags)
LIBS += $(shell $(PSPBIN)/freetype-config --libs)


include $(PSPSDK)/lib/build.mak
