NAME = apps
TWO_STAGE = 0

APP_SRCS = main gfx ui snake dict dictfile cards reader chess chess_engine
APP_OBJS1 = $(APP_SRCS:%=$(OBJDIR)/app/%.o)
APP_OBJS2 =

include ../buildinc.make

APP_CFLAGS += -std=c99 -pedantic
APP_CFLAGS += -I$(SYSDIR)


