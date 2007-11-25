#include <stdlib.h>
#include <g729a.h>
#include <inttypes.h>
#include <stdio.h>

static int16_t test_pattern[]={
0x6b21, 0x0050,
0x81, 0x7f, 
0x81, 0x7f,
0x81, 0x7f, 
0x7f, 0x7f,
0x81, 0x81,

0x7f, 0x7f,
0x81, 0x7f,
0x7f, 0x7f,
0x81, 0x7f,
0x7f, 0x7f,
0x81, 0x7f,

0x81, 0x81,
0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,

0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,
0x81, 0x81,
0x81, 0x81,
0x81, 0x7f,

0x81, 0x7f,
0x81, 0x81,
0x7f, 0x81,
0x81, 0x7f,
0x81, 0x7f,
0x7f, 0x81,

0x7f, 0x81,
0x81, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,
0x7f, 0x7f,

0x7f, 0x81,
0x81, 0x7f,
0x81, 0x7f,
0x81, 0x81,
0x7f, 0x7f,
};


#define L_FRAME 80
#define SERIAL_SIZE (L_FRAME+2)

int main(int argc, char** argv)
{
    FILE*f_serial;
    FILE* f_syn;
    void* ctx;
    short* buf;
    short serial[SERIAL_SIZE];
    serial,
    buf=calloc(1, 300);

  printf("\n");
  printf("************   G.729a 8.0 KBIT/S SPEECH DECODER  ************\n");
  printf("\n");
  printf("------------------- Fixed point C simulation ----------------\n");
  printf("\n");
  printf("-----------------          Version 1.1        ---------------\n");
  printf("\n");

   /* Passed arguments */

   if ( argc != 3)
     {
        printf("Usage :%s bitstream_file  outputspeech_file\n",argv[0]);
        printf("\n");
        printf("Format for bitstream_file:\n");
        printf("  One (2-byte) synchronization word \n");
        printf("  One (2-byte) size word,\n");
        printf("  80 words (2-byte) containing 80 bits.\n");
        printf("\n");
        printf("Format for outputspeech_file:\n");
        printf("  Synthesis is written to a binary file of 16 bits data.\n");
        exit( 1 );
     }

   /* Open file for synthesis and packed serial stream */

   if( (f_serial = fopen(argv[1],"rb") ) == NULL )
     {
        printf("%s - Error opening file  %s !!\n", argv[0], argv[1]);
        exit(0);
     }

   if( (f_syn = fopen(argv[2], "wb") ) == NULL )
     {
        printf("%s - Error opening file  %s !!\n", argv[0], argv[2]);
        exit(0);
     }

   printf("Input bitstream file  :   %s\n",argv[1]);
   printf("Synthesis speech file :   %s\n",argv[2]);

    ctx=g729a_decoder_init();

  while( fread(serial, sizeof(short), SERIAL_SIZE, f_serial) == SERIAL_SIZE)
  {
//    g729a_decode_frame(ctx, test_pattern, 0, buf, 0);
    g729a_decode_frame(ctx, serial, 0, buf, 0);
    fwrite(buf, sizeof(short), L_FRAME, f_syn);

  }
    g729a_decoder_uninit(ctx);
    free(buf);
    fclose(f_serial);
    fclose(f_syn);
    return 0;
}
