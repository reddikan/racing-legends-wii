#---------------------------------------------------------------------------------
# RACING LEGENDS - Wii homebrew (devkitPPC + libogc + libfat, GX graphics)
#
#   make        builds boot.dol (open it in Dolphin, or copy apps/racinglegends/ to an SD card)
#   make clean
#   make test   native test harness (needs only a C compiler; python3 + Pillow for PNGs)
#---------------------------------------------------------------------------------
ifeq ($(MAKECMDGOALS),test)
test:
	sh tools/run_tests.sh shots
.PHONY: test
else

ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC is not set. Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and run:  sudo dkp-pacman -S wii-dev   then   export DEVKITPPC=/opt/devkitpro/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITPPC)/bin:$(PATH)

TARGET  := boot
SRCDIR  := source
SRCS    := main game race hud menu car track scenery fx r3d ui audio font_bebas
OBJS    := $(addprefix $(SRCDIR)/,$(addsuffix .o,$(SRCS)))
HDRS    := $(wildcard $(SRCDIR)/*.h)

CFLAGS  := -O2 -Wall -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC) -I$(SRCDIR)
LDFLAGS := $(MACHDEP) -L$(LIBOGC_LIB)
LIBS    := -lfat -lasnd -lwiiuse -lbte -logc -lm

all: $(TARGET).dol
	mkdir -p apps/racinglegends
	cp $(TARGET).dol meta.xml icon.png apps/racinglegends/

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(OBJS): %.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET).elf $(TARGET).dol apps

.PHONY: all clean
endif
