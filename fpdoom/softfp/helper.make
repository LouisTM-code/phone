# soft float from LLVM compiler-rt (Apache 2.0 with LLVM exception): the phone's
# ARM926 has no FPU, the NDK's own builtins are Thumb-2 (ARMv7) code
CRT_VER = 14.0.6
CHECKSUM = 88df303840ca8fbff944e15e61c141226fe79f5d2b8e89fb024264d77841a02e
ARCHIVE = compiler-rt-$(CRT_VER).src.tar.xz

.PHONY: all
all: builtins

$(ARCHIVE):
	wget -O $@.tmp https://github.com/llvm/llvm-project/releases/download/llvmorg-$(CRT_VER)/$(ARCHIVE)
	test "$$(sha256sum $@.tmp | cut -d " " -f 1)" = $(CHECKSUM)
	mv $@.tmp $@

builtins: $(ARCHIVE)
	mkdir -p $@
	tar -xJf $< -C $@ --strip-components 3 --wildcards \
		"compiler-rt-$(CRT_VER).src/lib/builtins/*.c" "compiler-rt-$(CRT_VER).src/lib/builtins/*.h" \
		"compiler-rt-$(CRT_VER).src/lib/builtins/*.inc"
