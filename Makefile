#
# Unified Makefile for Virtual Jaguar GCC/SDL Portable Jaguar Emulator
#
# Note that SYSTYPE, EXESUFFIX, GLLIB, ICON, and SDLLIBTYPE are passed in by the
# script called "compile". The unified makefile should make life a little
# easier for the maintainers. :-)
#

CC         = gcc
LD         = gcc
TARGET     = vj$(EXESUFFIX)

CFLAGS = -Wall -O0 -g `sdl-config --cflags` -D__GCCUNIX__ -DDSP_EMU
LDFLAGS =

LIBS = `sdl-config --libs` -lz $(GLLIB)

INCS = -I. -Isrc -Isrc/include

THECC = $(CC) $(CFLAGS) $(INCS)

OBJS = \
	obj/anajoy.o \
	obj/blitter.o \
	obj/clock.o \
	obj/crc32.o \
	obj/dac.o \
	obj/dsp.o \
	obj/eeprom.o \
	obj/gpu.o \
	obj/gui.o \
	obj/jaguar.o \
	obj/jerry.o \
	obj/joystick.o \
	obj/m68kcpu.o obj/m68kops.o obj/m68kopac.o obj/m68kopdm.o obj/m68kopnz.o \
	obj/memory.o \
	obj/objectp.o \
	obj/settings.o \
	obj/tom.o \
	obj/unzip.o \
	obj/version.o \
	obj/video.o \
	obj/vj.o

all: obj $(TARGET)

clean:
	rm -rf obj
	rm -f ./vj$(EXESUFFIX)

obj:
	mkdir obj

obj/%.o: src/%.c src/include/%.h
	$(THECC) -c $< -o $@

obj/%.o: src/%.c
	$(THECC) -c $< -o $@


$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
#	strip --strip-all vj$(EXESUFFIX)
#	upx -9 vj$(EXESUFFIX)

# Other stuff that has unusual dependencies

obj/gui.o: src/gui.c src/include/gui.h
obj/cdintf.o: src/cdintf.c src/cdintf_linux.c src/include/cdintf.h

#
# Musashi specific stuffola
#

obj/m68kcpu.o: obj/m68kops.h src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Iobj -c src/m68kcpu.c -o obj/m68kcpu.o

obj/m68kops.o: obj/m68kmake$(EXESUFFIX) obj/m68kops.h obj/m68kops.c src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Isrc -c obj/m68kops.c -o obj/m68kops.o

obj/m68kopac.o: obj/m68kmake$(EXESUFFIX) obj/m68kops.h obj/m68kopac.c src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Isrc -c obj/m68kopac.c -o obj/m68kopac.o

obj/m68kopdm.o: obj/m68kmake$(EXESUFFIX) obj/m68kops.h obj/m68kopdm.c src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Isrc -c obj/m68kopdm.c -o obj/m68kopdm.o

obj/m68kopnz.o: obj/m68kmake$(EXESUFFIX) obj/m68kops.h obj/m68kopnz.c src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Isrc -c obj/m68kopnz.c -o obj/m68kopnz.o

obj/m68kdasm.o: src/m68kdasm.c src/m68k.h src/m68kconf.h
	$(CC) $(CFLAGS) -Isrc -c src/m68kdasm.c -o obj/m68kdasm.o

obj/m68kops.h: obj/m68kmake$(EXESUFFIX)
	obj/m68kmake obj src/m68k_in.c

obj/m68kmake$(EXESUFFIX): src/m68kmake.c src/m68k_in.c
	$(CC) $(WARNINGS) src/m68kmake.c -o obj/m68kmake$(EXESUFFIX)
