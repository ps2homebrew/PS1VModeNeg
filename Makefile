EE_BIN = PS1VModeNegRaw.elf
EE_BIN_PACKED = PS1VModeNeg.elf
EE_OBJS = main.o cnf_lite.o

EE_INCS := -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I.
EE_GPVAL = -G0
EE_CFLAGS = -D_EE -Os -mno-gpopt $(EE_GPVAL) -Wall $(EE_INCS)
EE_LIBS += -ldebug
EE_NEWLIB_NANO ?= 1
EE_COMPACT_EXECUTABLE ?= 1

EE_LINKFILE ?= linkfile

all: $(EE_BIN_PACKED)

$(EE_BIN_PACKED): $(EE_BIN)
	echo "Compressing..."
	ps2-packer $< $@
	rm -f $(EE_BIN)

clean:
	rm -f $(EE_OBJS) $(EE_BIN_PACKED)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.eeglobal