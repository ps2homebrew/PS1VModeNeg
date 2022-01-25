EE_BIN = PS1VModeNeg.elf
EE_OBJS = main.o cnf_lite.o

EE_INCS := -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I.
EE_GPVAL = -G0
EE_CFLAGS = -D_EE -Os -mno-gpopt $(EE_GPVAL) -Wall $(EE_INCS)
EE_LDFLAGS = -Tlinkfile -L$(PS2SDK)/ee/lib -s
EE_LIBS += -lcdvd -ldebug -lc -lkernel-nopatch

%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

$(EE_BIN) : $(EE_OBJS)
	$(EE_CC) $(EE_CFLAGS) $(EE_LDFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LIBS)

all: $(EE_BIN)

clean:
	rm -f $(EE_OBJS) $(EE_BIN)

include $(PS2SDK)/Defs.make
