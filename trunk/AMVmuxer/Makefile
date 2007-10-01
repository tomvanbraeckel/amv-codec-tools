patch=amvmux_patch.diff

all:
	@echo "usage: make [patch|build|diff|clean|test|debug]"

patch:
	svn revert -R ffmpeg
	svn update ffmpeg
	cd ffmpeg && patch -p1 < ../$(patch)
	ln -sf `pwd`/amvenc.c `pwd`/ffmpeg/libavformat/
	ln -sf `pwd`/amv.h `pwd`/ffmpeg/libavformat/
	cd ffmpeg && ./configure

diff:
	svn diff ffmpeg > $(patch).new
	diff -uw $(patch) $(patch).new | grep '^[+-]' | grep -v '[+-]Index:' | grep '^[+-]' && mv $(patch).new $(patch) && svn diff || rm $(patch).new


build:
	cd ffmpeg && make

clean:
	cd ffmpeg && make clean

test: build
	ffmpeg/ffmpeg -i hole.avi -f amv -intra -qscale 1 -r 16 -s 160x120 -ac 1 -ar 22050 -y hole.amv

debug: build
	echo "**** test build in gdb ****"
	gdb --args ffmpeg/ffmpeg_g -i hole.avi -f amv -intra -qscale 1 -r 16 -s 160x120 -ac 1 -ar 22050 -y hole.amv

.PHONY: all patch dif build clean test
