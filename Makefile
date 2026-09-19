TARGET = mkpsp
OBJS = src/main.o src/music.o src/sfx.o

# PSP-1000/1004 has 32 MiB of RAM; never request the Slim memory partition.
PSP_LARGE_MEMORY = 0

INCDIR =
CFLAGS = -O2 -G0 -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS = -lpspgu -lpspgum -lpspge -lpspdisplay -lpspctrl -lpsppower -lpspaudio -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = MKPSP Prototype

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
