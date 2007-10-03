all: build
	@echo "usage: make [build|clean|test|debug]"

configure:
	test -f ffmpeg/config.mak || ( cd ffmpeg && ./configure )

build: configure
	cd ffmpeg && make

clean:
	cd ffmpeg && make clean

test: build test-hole

test-%:
	ffmpeg/ffmpeg -i $*.avi -f amv -r 16 -s 160x120 -ac 1 -ar 22050 -y $*.amv
	ffmpeg/ffmpeg -i $*.amv -y test.avi

debug: build
	echo "**** test build in gdb ****"
	gdb --args ffmpeg/ffmpeg_g -i hole.avi -f amv -r 16 -s 160x120 -ac 1 -ar 22050 -y hole.amv

compare_amv: compare_amv.c
	gcc -o $@ $<

compare: compare_amv
	./compare_amv

.PHONY: all patch dif build clean test compare

