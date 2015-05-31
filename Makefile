TARGET = psppflight
OBJS = main.o psplog.o drone.o

CFLAGS = -g -O2 -G0 -Wall -Wextra
CXXFLAGS = -g -O2 -Wall -Wextra -fno-exceptions -fno-rtti

LIBS := \
	-larcommands \
	-larnetwork \
	-larnetworkal \
	-lardiscovery \
	-larstream \
	-larsal \
	-lpspgu \
	-lpsputility \

PSP_FW_VERSION = 635
BUILD_PRX=1

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Drone Control

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
