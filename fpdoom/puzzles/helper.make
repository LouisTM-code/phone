# Simon Tatham's Portable Puzzle Collection (MIT licence), a dated snapshot
PZ_VER = 20260923.616da16
CHECKSUM = cc419c8060b4e22be398aa03e30651fbce146eee510645c303fb6f5f1b77b78e
ARCHIVE = puzzles-$(PZ_VER).tar.gz

.PHONY: all
all: puzzles_src

$(ARCHIVE):
	wget -O $@.tmp https://www.chiark.greenend.org.uk/~sgtatham/puzzles/$(ARCHIVE)
	test "$$(sha256sum $@.tmp | cut -d " " -f 1)" = $(CHECKSUM)
	mv $@.tmp $@

puzzles_src: $(ARCHIVE)
	rm -rf $@.tmp && mkdir $@.tmp
	tar -xzf $< -C $@.tmp --strip-components 1
	patch -p1 -d $@.tmp < puzzles.patch
	mv $@.tmp $@
