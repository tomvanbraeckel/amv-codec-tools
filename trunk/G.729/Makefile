
LIBNAME   = libg729a.a
ENCODERNAME = coder
DECODERNAME = decoder

G729DIR := g729anxaE
AR= ar

CFLAGS += -Wall -D__unix -I$(G729DIR) -lm

COMMON-OBJECTS := BASIC_OP.o\
 BITS.o\
 DSPFUNC.o\
 FILTER.o\
 GAINPRED.o\
 LPCFUNC.o\
 LSPGETQ.o\
 OPER_32B.o\
 P_PARITY.o\
 PRED_LT3.o\
 TAB_LD8A.o\
 UTIL.o


ENCODER-OBJECTS := $(COMMON-OBJECTS) \
 ACELP_CA.o\
 COD_LD8A.o\
 LPC.o\
 PITCH_A.o\
 PRE_PROC.o\
 QUA_GAIN.o\
 QUA_LSP.o\
 TAMING.o\
 COR_FUNC.o

DECODER-OBJECTS := $(COMMON-OBJECTS) \
 DE_ACELP.o\
 DEC_GAIN.o\
 DEC_LAG3.o\
 DEC_LD8A.o\
 LSPDEC.o\
 POST_PRO.o\
 POSTFILT.o

DECODER-OBJECTS := $(patsubst %.o,$(G729DIR)/%.o, $(DECODER-OBJECTS))
ENCODER-OBJECTS := $(patsubst %.o,$(G729DIR)/%.o, $(ENCODER-OBJECTS))

LIB-OBJECTS := $(DECODER-OBJECTS) $(ENCODER-OBJECTS) g729a.o

DECODER-OBJECTS += $(G729DIR)/DECODER.o
ENCODER-OBJECTS += $(G729DIR)/CODER.o

DECODER-SRCS := $(patsubst %.o,%.c, $(DECODER-OBJECTS))
ENCODER-SRCS := $(patsubst %.o,%.c, $(ENCODER-OBJECTS))


all: prepare $(LIBNAME) $(ENCODERNAME) $(DECODERNAME) 

clean: 
	rm *.o
	rm $(G729DIR)/*o

distclean: clean
	rm $(LIBNAME)
	rm $(ENCODERNAME)
	rm $(DECODERNAME)

$(ENCODERNAME): $(ENCODER-OBJECTS)
	gcc $(CFLAGS) -o $@ $^

$(DECODERNAME) : $(DECODER-OBJECTS)
	gcc $(CFLAGS) -o $@ $^

# linker
$(LIBNAME) : $(LIB-OBJECTS)
	$(AR) rcs $(LIBNAME) $(LIB-OBJECTS)

g729a_native.o: g729a_native.c
	$(CC) $(CFLAGS) -c -o $@ $<

native: g729a_native.o
	$(AR) rcs $(LIBNAME) $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.h: %.H
	rename \.H \.h $<

%.c: %.C
	rename \.C \.c $<

prepare: sources.dep

sources.dep: $(DECODER-SRCS) $(ENCODER-SRCS)
	dos2unix $(G729DIR)/*.c
	dos2unix $(G729DIR)/*.h
	dos2unix $(G729DIR)/*.MAK
	touch sources.dep

ffmpeg-cfg:
	cd ../AMVmuxer/ffmpeg ; \
	./configure --enable-libg729a --extra-cflags="-I../../G.729 -I../../../G.729" --extra-ldflags=-L../../G.729

ffmpeg:
	make -C ../AMVmuxer/ffmpeg

test: ffmpeg test.act
	../AMVmuxer/ffmpeg/ffmpeg -i test.act test.wav

tests: all test_native test_orig 
	./test_orig
	./test_native

test_native: g729a_native.c test.c
	gcc $(CFLAGS) -lm -I. -o test_native $^

test_orig: test.c $(LIBNAME)
	gcc $(CFLAGS) -L. -I. -o test_orig $<  -lg729a

.PHONY: prepare all clean distclean ffmpeg native
