
LIBNAME   = libg729a.a
ENCODERNAME = coder
DECODERNAME = decoder

G729DIR := g729anxaE
AR= ar

CFLAGS += -D__unix -I$(G729DIR)

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

DECODER-OBJECTS += DECODER.o
ENCODER-OBJECTS += CODER.o

DECODER-OBJECTS := $(patsubst %.o,$(G729DIR)/%.o, $(DECODER-OBJECTS))
ENCODER-OBJECTS := $(patsubst %.o,$(G729DIR)/%.o, $(ENCODER-OBJECTS))

LIB-OBJECTS := $(DECODER-OBJECTS) $(ENCODER-OBJECTS) g729.o

LIB-OBJECTS := $(filter-out DECODER.O, $(LIB-OBJECTS))
LIB-OBJECTS := $(filter-out CODER.O, $(LIB-OBJECTS))

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
	gcc -o $@ $^

$(DECODERNAME) : $(DECODER-OBJECTS)
	gcc -o $@ $^

# linker
$(LIBNAME) : $(LIB-OBJECTS)
	$(AR) rcs $(LIBNAME) $(LIB-OBJECTS)

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
	./configure --extra-libs="-l g729a" --extra-cflags="-I../../G.729 -I../../../G.729" --extra-ldflags=-L../../G.729;\

ffmpeg:
	make -C ../AMVmuxer/ffmpeg

test: ffmpeg test.act
	../AMVmuxer/ffmpeg/ffmpeg -i test.act test.wav

.PHONY: prepare all clean distclean ffmpeg
