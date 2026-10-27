# Boot launcher, built like ../fpmenu (its loader parts are used from there)
NAME = launcher
TWO_STAGE = -1
LIBC_SDIO = 3

SYS_EXTRA = microfat sdio

APP_SRCS = main
EXTRA_SRCS = start3 readbin
APP_OBJS1 = $(APP_SRCS:%=$(OBJDIR)/app/%.o)
APP_OBJS2 = $(EXTRA_SRCS:%=$(OBJDIR)/app/%.o)

SYS_SRCS1 = start entry $(SYS_SRCS)
SYS_SRCS2 = asmcode common
OBJS1 = $(SYS_SRCS1:%=$(OBJDIR)/sys/%.o) $(APP_OBJS1)
OBJS2 = $(SYS_SRCS2:%=$(OBJDIR)/sys/%.o) $(APP_OBJS2)
OBJS = $(OBJS1) $(OBJS2)

vpath readbin.c ../fpmenu
vpath start3.s ../fpmenu

include ../buildinc.make

CFLAGS += -DSDIO_VERBOSE=0 -DSDIO_SHL=CHIPRAM_ADDR
CFLAGS := $(filter-out -DFAT_WRITE=1, $(CFLAGS))

APP_CFLAGS += -std=c99 -pedantic -I$(SYSDIR) -I../fpmenu

$(OBJDIR)/$(NAME)_part1.elf: $(OBJS1)
	$(CC) $(LFLAGS) $(OBJS1) -o $@

$(OBJDIR)/$(NAME)_part2.elf: $(OBJS2)
	$(CC) $(LFLAGS) $(OBJS2) -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary -j .text $< $@

$(NAME).bin: $(patsubst %,$(OBJDIR)/$(NAME)_part%,2.bin 1.bin 1.rel)
	cat $^ > $@
