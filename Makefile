# Makefile designed for OpenPS3FTP v3 by jjolano
# Simple Makefile for c/c++ code

ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT is not installed. Please install PSL1GHT.")
endif

# Basic configuration
TARGET		:= openps3ftp

TITLE		:= OpenPS3FTP
APPID		:= OFTP00001
CONTENTID	:= TOOL01-$(APPID)_00-E3D75E05E19B6152

ICON0		:= ./ICON0.PNG
SFOXML		:= ./sfo.xml

LIBS		:= -lrt -llv2 -lnet -lnetctl -lio -lrsx -lgcm_sys -lsysmodule -lsysutil

# Compile tools
MAKE_SELF	:= make_self_npdrm
MAKE_SFO	:= sfo.py
MAKE_PKG	:= pkg.py

# Files
CFILES		:= $(shell find * -name "*.c"   -print)
CPPFILES	:= $(shell find * -name "*.cpp" -print)

OBJS		:= ${CFILES:.c=.o} ${CPPFILES:.cpp=.o}

INCLUDES	:= -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include
LIBDIRS		:= -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib

# Rules
.PHONY: all clean

all: $(TARGET).elf

clean:
	rm -f $(OBJS) $(TARGET).elf

%.o: %.c
	@echo "$< -> $@"
	@ppu-gcc -Wall -O3 -std=gnu99 $(INCLUDES) -c $< -o $@

%.o: %.cpp
	@echo "$< -> $@"
	@ppu-g++ -Wall -O3 $(INCLUDES) -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo "$(OBJS) -> $@"
	@ppu-gcc $(OBJS) $(LIBDIRS) $(LIBS) -o $@

pkg: $(TARGET).elf
	@echo $(TARGET).pkg
	@rm -Rf ./temp
	@mkdir -p ./temp
	@mkdir -p ./temp/USRDIR
	@cp $(ICON0) ./temp
	@touch ./temp/USRDIR/firstrun
	@$(MAKE_SELF) $(TARGET).elf ./temp/USRDIR/EBOOT.BIN $(CONTENTID) >> /dev/null
	@$(MAKE_SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) ./temp/PARAM.SFO
	@$(MAKE_PKG) --contentid $(CONTENTID) ./temp/ $(TARGET).pkg >> /dev/null
	@rm -Rf ./temp

zip: pkg
	@echo $(TARGET).zip
	@zip -qul9 $(TARGET).zip -n pkg README COPYING ChangeLog $(TARGET).pkg

