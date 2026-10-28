# Soft float and math.h (libm.c) for a port built with TWO_STAGE = 0: include
# after ../buildinc.make, add -I../softfp to the flags of sources using math.h.
# Sources: make -C ../softfp -f helper.make
SOFTFP_DIR = ../softfp
SOFTFP_SRCS = adddf3 addsf3 subdf3 subsf3 muldf3 mulsf3 divdf3 divsf3 \
	comparedf2 comparesf2 extendsfdf2 truncdfsf2 negdf2 negsf2 \
	fixdfsi fixsfsi fixunsdfsi fixunssfsi fixdfdi fixsfdi fixunsdfdi fixunssfdi \
	floatsidf floatsisf floatunsidf floatunsisf floatdidf floatdisf \
	floatundidf floatundisf clzdi2 ctzsi2 fp_mode
SOFTFP_OBJS = $(SOFTFP_SRCS:%=$(OBJDIR)/softfp/%.o) $(OBJDIR)/softfp/aeabi_cmp.o \
	$(OBJDIR)/softfp/libm.o
APP_OBJS2 += $(SOFTFP_OBJS)
# OBJS was expanded into the link prerequisites already
$(OBJDIR)/$(NAME).elf: $(SOFTFP_OBJS)

SOFTFP_CFLAGS = -std=gnu11 -I$(SOFTFP_DIR)/builtins -Wno-unused-parameter \
	-Wno-sign-compare -Wno-missing-prototypes -fno-lto

$(OBJDIR)/softfp/%.o: $(SOFTFP_DIR)/builtins/%.c | objdir
	$(call compile_cc,$(SOFTFP_CFLAGS))
$(OBJDIR)/softfp/aeabi_cmp.o: $(SOFTFP_DIR)/aeabi_cmp.c | objdir
	$(call compile_cc,$(SOFTFP_CFLAGS))
$(OBJDIR)/softfp/libm.o: $(SOFTFP_DIR)/libm.c | objdir
	$(call compile_cc,-std=c99 -I$(SOFTFP_DIR))
