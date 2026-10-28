# Simon Tatham's puzzles, all in one program, see README.md
NAME = puzzles
TWO_STAGE = 0
PZDIR = puzzles_src

# the games listed in the collection's CMakeLists.txt
PZ_GAMES = $(filter-out nullgame,$(shell grep -o '^puzzle.[a-z]*' $(PZDIR)/CMakeLists.txt | cut -c8-))
PZ_CORE = combi divvy draw-poly drawing dsf findloop grid latin laydomino \
	loopgen malloc matching midend misc penrose penrose-legacy ps random sort \
	tdq tree234 version hat spectre list

APP_SRCS = puzzles_fp pz_ui term gfx port_libc
APP_OBJS1 = $(APP_SRCS:%=$(OBJDIR)/app/%.o)
APP_OBJS2 = $(PZ_CORE:%=$(OBJDIR)/pz/%.o) $(PZ_GAMES:%=$(OBJDIR)/pz/%.o)

vpath term.c ../term
vpath port_libc.c ../term
vpath gfx.c ../apps

include ../buildinc.make
include ../softfp/softfp.make

# the puzzles' draw_text() would clash with the one of ../apps/gfx.c;
# math.h also declares atof(), which the fpdoom stdlib.h lacks
PZ_CFLAGS = -std=c99 -I$(SYSDIR) -I../softfp -include math.h -I$(PZDIR) -I$(OBJDIR)/gen -DCOMBINED \
	-Ddraw_text=pz_draw_text \
	-Wno-unused -Wno-sign-compare -Wno-unused-parameter \
	-Wno-implicit-fallthrough -Wno-missing-field-initializers
APP_CFLAGS += -std=c99 -I$(SYSDIR) -I../term -I../apps
$(OBJDIR)/app/puzzles_fp.o: APP_CFLAGS += $(PZ_CFLAGS)

$(OBJDIR)/gen/generated-games.h: $(PZDIR)/CMakeLists.txt | objdir
	mkdir -p $(dir $@)
	for g in $(PZ_GAMES); do echo "GAME($$g)"; done > $@

$(OBJDIR)/pz/%.o: $(PZDIR)/%.c $(OBJDIR)/gen/generated-games.h | objdir
	$(call compile_cc,$(PZ_CFLAGS))
$(OBJDIR)/app/puzzles_fp.o: $(OBJDIR)/gen/generated-games.h
