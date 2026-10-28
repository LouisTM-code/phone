NAME = apps
TWO_STAGE = 0

APP_SRCS = main gfx ui dict dictfile cards reader book

# temporary debug log (log.h); after changing LOG run make clean
LOG ?= 1
ifeq ($(LOG), 1)
APP_SRCS += log
EXTRA_CFLAGS += -DAPP_LOG=1 -DATEXIT_MAX=1
endif
# the PC build checks what the phone's card allows and counts reads (pccard.c)
ifeq ($(PCTEST), 1)
APP_SRCS += pccard
LFLAGS += $(foreach f,fopen fread fwrite malloc calloc realloc free,-Wl,--wrap=$(f))
endif
APP_OBJS1 = $(APP_SRCS:%=$(OBJDIR)/app/%.o)
APP_OBJS2 =

include ../buildinc.make

APP_CFLAGS += -std=c99 -pedantic
APP_CFLAGS += -I$(SYSDIR)


