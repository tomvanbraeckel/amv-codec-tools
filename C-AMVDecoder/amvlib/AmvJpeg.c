#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AmvDec.h"
#include "AmvJpeg.h"


//for C linkage
#ifdef __cplusplus
extern "C" {
#endif
	
/* These are the sample quantization tables given in JPEG spec section K.1.
* The spec says that the values given produce "good" quality, and
* when divided by 2, "very good" quality.
*/
static const unsigned char std_luminance_quant_tbl[64] = {
		16,  11,  10,  16,  24,  40,  51,  61,
		12,  12,  14,  19,  26,  58,  60,  55,
		14,  13,  16,  24,  40,  57,  69,  56,
		14,  17,  22,  29,  51,  87,  80,  62,
		18,  22,  37,  56,  68, 109, 103,  77,
		24,  35,  55,  64,  81, 104, 113,  92,
		49,  64,  78,  87, 103, 121, 120, 101,
		72,  92,  95,  98, 112, 100, 103,  99
};

static const unsigned char amv_luminance_quant_tbl[64] = {
		0x08,  0x06,  0x06,  0x07,  0x06,  0x05,  0x08,  0x07,
		0x07,  0x07,  0x09,  0x09,  0x08,  0x0A,  0x0C,  0x14,
		0x0D,  0x0C,  0x0B,  0x0B,  0x0C,  0x19,  0x12,  0x13,
		0x0F,  0x14,  0x1D,  0x1A,  0x1F,  0x1E,  0x1D,  0x1A,
		0x1C,  0x1C,  0x20,  0x24,  0x2E,  0x27,  0x20,  0x22,
		0x2C,  0x27,  0x1C,  0x1C,  0x28,  0x37,  0x29,  0x2C,
		0x30,  0x31,  0x34,  0x34,  0x34,  0x1F,  0x27,  0x39,
		0x3D,  0x38,  0x32,  0x3C,  0x2E,  0x33,  0x34,  0x32
};

static const unsigned char std_chrominance_quant_tbl[64] = {
		17,  18,  24,  47,  99,  99,  99,  99,
		18,  21,  26,  66,  99,  99,  99,  99,
		24,  26,  56,  99,  99,  99,  99,  99,
		47,  66,  99,  99,  99,  99,  99,  99,
		99,  99,  99,  99,  99,  99,  99,  99,
		99,  99,  99,  99,  99,  99,  99,  99,
		99,  99,  99,  99,  99,  99,  99,  99,
		99,  99,  99,  99,  99,  99,  99,  99
};

static const unsigned char amv_chrominance_quant_tbl[64] = {
		0x09,  0x09,  0x09,  0x0C,  0x0B,  0x0C,  0x18,  0x0D,
		0x0D,  0x18,  0x32,  0x21,  0x1C,  0x21,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,
		0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32,  0x32
};

/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
static const unsigned char bits_dc_luminance[17] =
{ /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };

static const unsigned char val_dc_luminance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const unsigned char bits_dc_chrominance[17] =
{ /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };

static const unsigned char val_dc_chrominance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const unsigned char bits_ac_luminance[17] =
{ /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };

static const unsigned char val_ac_luminance[] =
{
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

static const unsigned char bits_ac_chrominance[17] =
{ /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };

static const unsigned char val_ac_chrominance[] =
{
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

static const int Zig_Zag[8][8] =
{
	{0, 1, 5, 6, 14, 15, 27, 28},
	{2, 4, 7, 13, 16, 26, 29, 42},
	{3, 8, 12, 17, 25, 30, 41, 43},
	{9, 11, 18, 24, 37, 40, 44, 53},
	{10, 19, 23, 32, 39, 45, 52, 54},
	{20, 22, 33, 38, 46, 51, 55, 60},
	{21, 34, 37, 47, 50, 56, 59, 61},
	{35, 36, 48, 49, 57, 58, 62, 63}
};

#define W1 2841 /* 2048*sqrt(2)*cos(1*pi/16) */
#define W2 2676 /* 2048*sqrt(2)*cos(2*pi/16) */
#define W3 2408 /* 2048*sqrt(2)*cos(3*pi/16) */
#define W5 1609 /* 2048*sqrt(2)*cos(5*pi/16) */
#define W6 1108 /* 2048*sqrt(2)*cos(6*pi/16) */
#define W7 565  /* 2048*sqrt(2)*cos(7*pi/16) */



static void AmvJpeg_PutMarker(FILE *fp, JPEG_MARKER maker)
{
	unsigned char buftmp[2];

	buftmp[0] = 0xff;
	buftmp[1] = maker;
	fwrite(buftmp, 2, 1, fp);
}

static void AmvJpeg_JpegDQTTableHeader(FILE *fp)
{
    int i;
	unsigned char chtmp[2];
	unsigned char luminance_quant_tbl[64];
	unsigned char chrominance_quant_tbl[64];

//	unsigned char *ptr;
	
    /* quant matrixes */
    AmvJpeg_PutMarker(fp, DQT);

	i = 2 + 1 * (1 + 64);
	chtmp[0] = (unsigned char)((i & 0xff00)>>8);
	chtmp[1] = (unsigned char)(i & 0xff);
	fwrite(chtmp, 2, 1, fp);
//	put_bits(p, 16, 2 + 1 * (1 + 64));

	chtmp[0] = 0;
	fwrite(chtmp, 1, 1, fp);
//	put_bits(p, 4, 0); /* 8 bit precision */
//	put_bits(p, 4, 0); /* table 0 */
	
	for(i=0; i<64; i++)
		luminance_quant_tbl[i] = std_luminance_quant_tbl[i]/2;
	fwrite(amv_luminance_quant_tbl, 64, 1, fp);
//	for(i=0; i<64; i++) {
//		j = s->intra_scantable.permutated[i];
//		put_bits(p, 8, s->intra_matrix[j]);
//	}
	

	/* quant matrixes */
    AmvJpeg_PutMarker(fp, DQT);
	
	i = 2 + 1 * (1 + 64);
	chtmp[0] = (unsigned char)((i & 0xff00)>>8);
	chtmp[1] = (unsigned char)(i & 0xff);
	fwrite(chtmp, 2, 1, fp);
	//	put_bits(p, 16, 2 + 1 * (1 + 64));
	
	chtmp[0] = 1;
	fwrite(chtmp, 1, 1, fp);
	//	put_bits(p, 4, 0); /* 8 bit precision */
	//	put_bits(p, 4, 1); /* table 0 */
	
	for(i=0; i<64; i++)
		chrominance_quant_tbl[i] = std_chrominance_quant_tbl[i]/2;
	fwrite(amv_chrominance_quant_tbl, 64, 1, fp);
	//	for(i=0; i<64; i++) {
	//		j = s->intra_scantable.permutated[i];
	//		put_bits(p, 8, s->intra_matrix[j]);
	//	}
}

/* table_class: 0 = DC coef, 1 = AC coefs */
static int AmvJpeg_PutHuffmanTable(FILE *fp, int table_class, int table_id,
								   const unsigned char *bits_table, const unsigned char *value_table)
{
	unsigned char chtmp;
    int n, i;
	
	chtmp = (table_class<<4)|table_id;
	fwrite(&chtmp, 1, 1, fp);
	//  put_bits(p, 4, table_class);
	//  put_bits(p, 4, table_id);
	
    n = 0;
    for(i=1; i<=16; i++)
	{
		n += bits_table[i];
		fwrite(&bits_table[i], 1, 1, fp);
		//		put_bits(p, 8, bits_table[i]);
    }
	
    for(i=0; i<n; i++)
		fwrite(&value_table[i], 1, 1, fp);
	//		put_bits(p, 8, value_table[i]);
	
    return (n + 17);
}

static void AmvJpeg_JpegHuffmanTableHeader(FILE *fp)
{
	unsigned char chtmp[2];

    /* huffman table */
	AmvJpeg_PutMarker(fp, DHT);
	chtmp[0] = 0x00;
	chtmp[1] = 0x1F;
	fwrite(chtmp, 2, 1, fp);
	AmvJpeg_PutHuffmanTable(fp, 0, 0, bits_dc_luminance, val_dc_luminance);

	AmvJpeg_PutMarker(fp, DHT);
	chtmp[0] = 0x00;
	chtmp[1] = 0xB5;
	fwrite(chtmp, 2, 1, fp);
	AmvJpeg_PutHuffmanTable(fp, 1, 0, bits_ac_luminance, val_ac_luminance);

	AmvJpeg_PutMarker(fp, DHT);
	chtmp[0] = 0x00;
	chtmp[1] = 0x1F;
	fwrite(chtmp, 2, 1, fp);
	AmvJpeg_PutHuffmanTable(fp, 0, 1, bits_dc_chrominance, val_dc_chrominance);

	AmvJpeg_PutMarker(fp, DHT);
	chtmp[0] = 0x00;
	chtmp[1] = 0xB5;
	fwrite(chtmp, 2, 1, fp);
	AmvJpeg_PutHuffmanTable(fp, 1, 1, bits_ac_chrominance, val_ac_chrominance);
}


static void AmvJpeg_JpegPutComments(FILE *fp)
{
	unsigned char chbuf[12];
	
    /* JFIF header */
	AmvJpeg_PutMarker(fp, APP0);
	chbuf[0] = 0x00;
	chbuf[1] = 0x10;
	fwrite(chbuf, 2, 1, fp);
//	put_bits(p, 16, 16);

	chbuf[0] = 'J';
	chbuf[1] = 'F';
	chbuf[2] = 'I';
	chbuf[3] = 'F';
	chbuf[4] = 0x00;
	fwrite(chbuf, 5, 1, fp);
//	ff_put_string(p, "JFIF", 1); /* this puts the trailing zero-byte too */

	chbuf[0] = 0x01;
	chbuf[1] = 0x01;	// v 1.01
	fwrite(chbuf, 2, 1, fp);
//	put_bits(p, 16, 0x0201); /* v 1.02 */

	chbuf[0] = 0x01;
	chbuf[1] = 0x00;
	chbuf[2] = 0x60;
	chbuf[3] = 0x00;
	chbuf[4] = 0x60;
	chbuf[5] = 0x00;
	chbuf[6] = 0x00;
	fwrite(chbuf, 7, 1, fp);
//	put_bits(p, 8, 0); /* units type: 0 - aspect ratio */
//	put_bits(p, 16, s->avctx->sample_aspect_ratio.num);
//	put_bits(p, 16, s->avctx->sample_aspect_ratio.den);
//	put_bits(p, 8, 0); /* thumbnail width */
//	put_bits(p, 8, 0); /* thumbnail height */
}

void AmvJpegPutHeader(FILE *fp, unsigned short height, unsigned short width)
{
	unsigned char chbuf[16];

    AmvJpeg_PutMarker(fp, SOI);
	AmvJpeg_JpegPutComments(fp);
	
	AmvJpeg_JpegDQTTableHeader(fp);

	AmvJpeg_PutMarker(fp, SOF0 );
	chbuf[0] = 0;
	chbuf[1] = 17;
	chbuf[2] = 8;
	chbuf[3] = (unsigned char)((height&0xff00)>>8);
	chbuf[4] = (unsigned char)(height&0xff);
	chbuf[5] = (unsigned char)((width&0xff00)>>8);
	chbuf[6] = (unsigned char)(width&0xff);
	chbuf[7] = 3;
	fwrite(chbuf, 8, 1, fp);
//	put_bits(&s->pb, 16, 17);
//	put_bits(&s->pb, 8, 8); /* 8 bits/component */
//	put_bits(&s->pb, 16, s->height);
//	put_bits(&s->pb, 16, s->width);
//	put_bits(&s->pb, 8, 3); /* 3 components */
		
	/* Y component */
	chbuf[0] = 1;
	chbuf[1] = 0x22;
	chbuf[2] = 0;
	fwrite(chbuf, 3, 1, fp);
//	put_bits(&s->pb, 8, 1); /* component number */
//	put_bits(&s->pb, 4, s->mjpeg_hsample[0]); /* H factor */
//	put_bits(&s->pb, 4, s->mjpeg_vsample[0]); /* V factor */
//	put_bits(&s->pb, 8, 0); /* select matrix */
		
	/* Cb component */
	chbuf[0] = 2;
	chbuf[1] = 0x11;
	chbuf[2] = 1;
	fwrite(chbuf, 3, 1, fp);
//	put_bits(&s->pb, 8, 2); /* component number */
//	put_bits(&s->pb, 4, s->mjpeg_hsample[1]); /* H factor */
//	put_bits(&s->pb, 4, s->mjpeg_vsample[1]); /* V factor */
//	put_bits(&s->pb, 8, 0); /* select matrix */
		
	/* Cr component */
	chbuf[0] = 3;
	chbuf[1] = 0x11;
	chbuf[2] = 1;
	fwrite(chbuf, 3, 1, fp);
//	put_bits(&s->pb, 8, 3); /* component number */
//	put_bits(&s->pb, 4, s->mjpeg_hsample[2]); /* H factor */
//	put_bits(&s->pb, 4, s->mjpeg_vsample[2]); /* V factor */
//	put_bits(&s->pb, 8, 0); /* select matrix */
	
	AmvJpeg_JpegHuffmanTableHeader(fp);

    /* scan header */
    AmvJpeg_PutMarker(fp, SOS);
	chbuf[0] = 0;
	chbuf[1] = 12;
	chbuf[2] = 3;
	fwrite(chbuf, 3, 1, fp);
//	put_bits(&s->pb, 16, 12); /* length */
//	put_bits(&s->pb, 8, 3); /* 3 components */
	
    /* Y component */
	chbuf[0] = 1;
	chbuf[1] = 0;
	fwrite(chbuf, 2, 1, fp);
//	put_bits(&s->pb, 8, 1); /* index */
//	put_bits(&s->pb, 4, 0); /* DC huffman table index */
//	put_bits(&s->pb, 4, 0); /* AC huffman table index */
	
    /* Cb component */
	chbuf[0] = 2;
	chbuf[1] = 0x11;
	fwrite(chbuf, 2, 1, fp);
//	put_bits(&s->pb, 8, 2); /* index */
//	put_bits(&s->pb, 4, 1); /* DC huffman table index */
//	put_bits(&s->pb, 4, lossless ? 0 : 1); /* AC huffman table index */
	
    /* Cr component */
	chbuf[0] = 3;
	chbuf[1] = 0x11;
	fwrite(chbuf, 2, 1, fp);
//	put_bits(&s->pb, 8, 3); /* index */
//	put_bits(&s->pb, 4, 1); /* DC huffman table index */
//	put_bits(&s->pb, 4, lossless ? 0 : 1); /* AC huffman table index */
	
	chbuf[0] = 0;
	chbuf[1] = 63;
	chbuf[2] = 0;
	fwrite(chbuf, 3, 1, fp);
//	put_bits(&s->pb, 8, (lossless && !ls) ? s->avctx->prediction_method+1 : 0); /* Ss (not used) */
//	put_bits(&s->pb, 8, 63); break; /* Se (not used) */
//	put_bits(&s->pb, 8, 0); /* Ah/Al (not used) */
	
    //FIXME DC/AC entropy table selectors stuff in jpegls
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#define WIDTHBYTES(i)	((i+31)/32*4)
#define PI				3.1415926535
//define return value of function
#define FUNC_OK				0
#define FUNC_MEMORY_ERROR	1
#define FUNC_FILE_ERROR		2
#define FUNC_FORMAT_ERROR	3


DWORD           LineBytes;
LPSTR           lpPtr;
unsigned long	ImgWidth = 0 , ImgHeight = 0;

short			SampRate_Y_H, SampRate_Y_V;
short			SampRate_U_H, SampRate_U_V;
short			SampRate_V_H, SampRate_V_V;
short			H_YtoU, V_YtoU, H_YtoV, V_YtoV;
short			Y_in_MCU, U_in_MCU, V_in_MCU;
unsigned char   *lpJpegBuf;
unsigned char   *lp;
short			qt_table[3][64];
short			comp_num;
unsigned char	comp_index[3];
unsigned char	YDcIndex, YAcIndex, UVDcIndex, UVAcIndex;
unsigned char	HufTabIndex;
short		    *YQtTable, *UQtTable, *VQtTable;
unsigned char	And[9] = {0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff};
short		    code_pos_table[4][16], code_len_table[4][16];
unsigned short	code_value_table[4][256];
unsigned short	huf_max_value[4][16], huf_min_value[4][16];
short			BitPos, CurByte;
short			rrun, vvalue;
short			MCUBuffer[10*64];
int				QtZzMCUBuffer[10*64];
short			BlockBuffer[64];
short			ycoef, ucoef, vcoef;
int				IntervalFlag;
short			interval = 0;
int				Y[4*64], U[4*64], V[4*64];
unsigned long   sizei, sizej;
short 			restart;
static  long	iclip[1024];
static  long	*iclp;

static int InitTag();
static void InitTable();
static void GetYUV(short flag);
static void StoreBuffer();
static int DecodeElement();
static int HufBlock(unsigned char dchufindex, unsigned char achufindex);
static void IQtIZzMCUComponent(short flag);
static void IQtIZzBlock(short  *s, int *d, short flag);
static void Fast_IDCT(int * block);
static unsigned char ReadByte();
static void Initialize_Fast_IDCT();
static void idctrow(int * blk);
static void idctcol(int * blk);
static int DecodeMCUBlock();
static int Decode();

static int InitTag()
{
	int finish = 0;
	unsigned char id;
	short  llength;
	short  i,j,k;
	short  huftab1, huftab2;
	short  huftabindex;
	unsigned char hf_table_index;
	unsigned char qt_table_index;
	unsigned char comnum;

	unsigned char  *lptemp;
	short  ccount;

	lp = lpJpegBuf + 2;

	while(!finish)
	{
		id = *(lp+1);
		lp += 2;
		switch(id)
		{
		case APP0:
			llength = MAKEWORD(*(lp+1), *lp);
			lp += llength;
			break;
		case DQT:
			llength = MAKEWORD(*(lp+1), *lp);
			qt_table_index = (*(lp+2))&0x0f;
			lptemp = lp + 3;
			if(llength < 80)
			{
				for(i=0; i<64; i++)
					qt_table[qt_table_index][i] = (short)*(lptemp++);
			}
			else
			{
				for(i=0; i<64; i++)
					qt_table[qt_table_index][i] = (short)*(lptemp++);
                qt_table_index = (*(lptemp++))&0x0f;
  				for(i=0; i<64; i++)
					qt_table[qt_table_index][i] = (short)*(lptemp++);
  			}
  			lp += llength;
			break;
		case SOF0:
	 		llength = MAKEWORD(*(lp+1), *lp);
	 		ImgHeight = MAKEWORD(*(lp+4), *(lp+3));
	 		ImgWidth = MAKEWORD(*(lp+6), *(lp+5));
            comp_num = *(lp+7);
		    if((comp_num != 1) && (comp_num != 3))
	  			return FUNC_FORMAT_ERROR;
			if(comp_num == 3)
			{
				comp_index[0] = *(lp+8);
	  			SampRate_Y_H = (*(lp+9))>>4;
	  			SampRate_Y_V = (*(lp+9))&0x0f;
	  			YQtTable = (short *)qt_table[*(lp+10)];

				comp_index[1] = *(lp+11);
				SampRate_U_H = (*(lp+12))>>4;
	  			SampRate_U_V = (*(lp+12))&0x0f;
	  			UQtTable = (short *)qt_table[*(lp+13)];

	  			comp_index[2] = *(lp+14);
	  			SampRate_V_H = (*(lp+15))>>4;
	  			SampRate_V_V = (*(lp+15))&0x0f;
				VQtTable = (short *)qt_table[*(lp+16)];
	  		}
			else
			{
	  			comp_index[0] = *(lp+8);
				SampRate_Y_H = (*(lp+9))>>4;
	  			SampRate_Y_V = (*(lp+9))&0x0f;
	  			YQtTable = (short *)qt_table[*(lp+10)];

				comp_index[1] = *(lp+8);
	  			SampRate_U_H = 1;
	  			SampRate_U_V = 1;
	  			UQtTable = (short *)qt_table[*(lp+10)];

				comp_index[2] = *(lp+8);
				SampRate_V_H = 1;
	  			SampRate_V_V = 1;
	  			VQtTable = (short *)qt_table[*(lp+10)];
			}
  			lp += llength;						    
			break;
		case DHT:             
			llength = MAKEWORD(*(lp+1), *lp);
			if(llength < 0xd0)
			{
				huftab1 = (short)(*(lp+2))>>4;     //huftab1=0,1
		 		huftab2 = (short)(*(lp+2))&0x0f;   //huftab2=0,1
				huftabindex = huftab1*2 + huftab2;
		 		lptemp = lp + 3;
				for(i=0; i<16; i++)
					code_len_table[huftabindex][i] = (short)(*(lptemp++));
				j = 0;
				for(i=0; i<16; i++)
					if(code_len_table[huftabindex][i] != 0)
					{
						k = 0;
						while(k < code_len_table[huftabindex][i])
						{
							code_value_table[huftabindex][k+j] = (short)(*(lptemp++));
							k++;
						}
						j += k;	
					}
				i = 0;
				while(code_len_table[huftabindex][i] == 0)
		 			i++;
				for(j=0; j<i; j++)
				{
					huf_min_value[huftabindex][j] = 0;
					huf_max_value[huftabindex][j] = 0;
				}
				huf_min_value[huftabindex][i] = 0;
				huf_max_value[huftabindex][i] = code_len_table[huftabindex][i]-1;
				for(j=i+1; j<16; j++)
				{
					huf_min_value[huftabindex][j] = (huf_max_value[huftabindex][j-1]+1)<<1;
					huf_max_value[huftabindex][j] = huf_min_value[huftabindex][j] + code_len_table[huftabindex][j]-1;
				}
				code_pos_table[huftabindex][0] = 0;
				for(j=1; j<16; j++)
		  			code_pos_table[huftabindex][j] = code_len_table[huftabindex][j-1] + code_pos_table[huftabindex][j-1];
		  		lp += llength;
			}  //if
			else
			{
	 			hf_table_index = *(lp+2);
				lp += 2;
				while(hf_table_index != 0xff)
				{
					huftab1 = (short)hf_table_index>>4;     //huftab1=0,1
			 		huftab2 = (short)hf_table_index&0x0f;   //huftab2=0,1
					huftabindex = huftab1*2 + huftab2;
					lptemp = lp + 1;
					ccount = 0;
					for(i=0; i<16; i++){
						code_len_table[huftabindex][i] = (short)(*(lptemp++));
						ccount += code_len_table[huftabindex][i];
					}
					ccount += 17;	
					j = 0;
					for(i=0; i<16; i++)
						if(code_len_table[huftabindex][i] != 0)
						{
							k = 0;
							while(k < code_len_table[huftabindex][i])
							{
								code_value_table[huftabindex][k+j] = (short)(*(lptemp++));
								k++;
							}
							j += k;
						}
					i = 0;
					while(code_len_table[huftabindex][i] == 0)
						i++;
					for(j=0; j<i; j++)
					{
						huf_min_value[huftabindex][j] = 0;
						huf_max_value[huftabindex][j] = 0;
					}
					huf_min_value[huftabindex][i] = 0;
					huf_max_value[huftabindex][i] = code_len_table[huftabindex][i] - 1;
					for(j=i+1; j<16; j++) {
						huf_min_value[huftabindex][j] = (huf_max_value[huftabindex][j-1]+1)<<1;
						huf_max_value[huftabindex][j] = huf_min_value[huftabindex][j] + code_len_table[huftabindex][j] - 1;
					}
					code_pos_table[huftabindex][0] = 0;
					for(j=1; j<16; j++)
						code_pos_table[huftabindex][j] = code_len_table[huftabindex][j-1] + code_pos_table[huftabindex][j-1];
					lp += ccount;
					hf_table_index = *lp;
				}  //while
			}  //else
			break;
		case DRI:
			llength = MAKEWORD(*(lp+1), *lp);
			restart = MAKEWORD(*(lp+3), *(lp+2));
			lp += llength;
			break;
		case SOS:
			llength = MAKEWORD(*(lp+1),*lp);
			comnum = *(lp+2);
			if(comnum != comp_num)
				return FUNC_FORMAT_ERROR;
			lptemp = lp + 3;
			for(i=0; i<comp_num; i++)
			{
				if(*lptemp == comp_index[0])
				{
					YDcIndex = (*(lptemp+1))>>4;   //Y
					YAcIndex = ((*(lptemp+1))&0x0f)+2;
				}
				else
				{
					UVDcIndex = (*(lptemp+1))>>4;   //U,V
					UVAcIndex = ((*(lptemp+1))&0x0f) + 2;
				}
				lptemp += 2;
			}
			lp += llength;
			finish = 1;
			break;
		case EOI:    
			return FUNC_FORMAT_ERROR;
			break;
		default:
 			if((id&0xf0) != 0xd0)
			{
				llength = MAKEWORD(*(lp+1), *lp);
	 			lp += llength;
			}
			else
				lp += 2;
			break;
  		}  //switch
	} //while

	

	return FUNC_OK;
}

static void InitTable()
{
	short i, j;
	sizei = sizej=0;
	ImgWidth = ImgHeight = 0;
	rrun = vvalue = 0;
	BitPos = 0;
	CurByte = 0;
	IntervalFlag = 0;
	restart = 0;
	for(i=0; i<3; i++)
		for(j=0; j<64; j++)
			qt_table[i][j] = 0;
	comp_num = 0;
	HufTabIndex = 0;
	for(i=0; i<3; i++)
		comp_index[i] = 0;
	for(i=0; i<4; i++)
		for(j=0; j<16; j++)
		{
			code_len_table[i][j] = 0;
			code_pos_table[i][j] = 0;
			huf_max_value[i][j] = 0;
			huf_min_value[i][j] = 0;
		}
		
	for(i=0; i<4; i++)
		for(j=0; j<256; j++)
			code_value_table[i][j] = 0;
			
	for(i=0; i<10*64; i++)
	{
		MCUBuffer[i] = 0;
		QtZzMCUBuffer[i] = 0;
	}
	for(i=0; i<64; i++)
	{
		Y[i] = 0;
		U[i] = 0;
		V[i] = 0;
		BlockBuffer[i] = 0;
	}
	ycoef = ucoef = vcoef = 0;
}

static void GetYUV(short flag)
{
	short H, VV;
	short i, j, k, h;
	int *buf;
	int *pQtZzMCU;

	switch(flag)
	{
	case 0:
		H = SampRate_Y_H;
		VV = SampRate_Y_V;
		buf = Y;
		pQtZzMCU = QtZzMCUBuffer;
		break;
	case 1:
		H = SampRate_U_H;
		VV = SampRate_U_V;
		buf = U;
		pQtZzMCU = QtZzMCUBuffer+Y_in_MCU*64;
		break;
	case 2:
		H = SampRate_V_H;
		VV = SampRate_V_V;
		buf = V;
		pQtZzMCU = QtZzMCUBuffer + (Y_in_MCU + U_in_MCU)*64;
		break;
	}
	for(i=0; i<VV; i++)
		for(j=0; j<H; j++)
			for(k=0; k<8; k++)
				for(h=0; h<8; h++)
					buf[(i*8+k) * SampRate_Y_H*8 + j*8 + h] = *pQtZzMCU++;
}

static void StoreBuffer()
{
	short i, j;
	unsigned char *lpbmp;
	unsigned char R, G, B;
	int y, u, v, rr, gg, bb;

	for(i=0; i<SampRate_Y_V*8; i++)
	{
		if((sizei+i) < ImgHeight)
		{
			lpbmp = ((unsigned char *)lpPtr + (unsigned long)(ImgHeight-sizei-i-1)*LineBytes+sizej*3);
			for(j=0; j<SampRate_Y_H*8; j++)
			{
				if((sizej+j) < ImgWidth)
				{
					y = Y[i * 8 * SampRate_Y_H + j];
					u = U[(i/V_YtoU) * 8 * SampRate_Y_H + j/H_YtoU];
					v = V[(i/V_YtoV) * 8 * SampRate_Y_H + j/H_YtoV];
					rr = ((y<<8) + 18*u + 367*v) >> 8;
					gg = ((y<<8) - 159*u - 220*v) >> 8;
					bb = ((y<<8) + 411*u - 29*v) >> 8;
					R = (unsigned char)rr;
					G = (unsigned char)gg;
					B = (unsigned char)bb;
					if(rr & 0xffffff00)
						if(rr > 255)
							R = 255;
						else if(rr < 0)
							R=0;
					if(gg & 0xffffff00)
						if(gg > 255)
							G = 255;
						else if(gg < 0)
							G = 0;
					if(bb & 0xffffff00)
						if(bb > 255)
							B = 255;
						else if(bb < 0)
							B = 0;
					*lpbmp++ = B;
					*lpbmp++ = G;
					*lpbmp++ = R;
				}
				else
					break;
			}
		}
		else
			break;
	}
}

static int DecodeElement()
{
	int thiscode, tempcode;
	unsigned short temp, valueex;
	short codelen;
	unsigned char hufexbyte, runsize, tempsize, sign;
	unsigned char newbyte, lastbyte;

	if(BitPos >= 1)
	{
		BitPos--;
		thiscode = (unsigned char)CurByte>>BitPos;
		CurByte = CurByte & And[BitPos];
	}
	else
	{
		lastbyte = ReadByte();
		BitPos--;
		newbyte = CurByte & And[BitPos];
		thiscode = lastbyte >> 7;
		CurByte = newbyte;
	}

	codelen = 1;
	
	while((thiscode < huf_min_value[HufTabIndex][codelen-1])||
		  (code_len_table[HufTabIndex][codelen-1] == 0)||
		  (thiscode > huf_max_value[HufTabIndex][codelen-1]))
	{
		if(BitPos >= 1)
		{
			BitPos--;
			tempcode = (unsigned char)CurByte>>BitPos;
			CurByte = CurByte & And[BitPos];
		}
		else
		{
			lastbyte = ReadByte();
			BitPos--;
			newbyte = CurByte & And[BitPos];
			tempcode = (unsigned char)lastbyte>>7;
			CurByte = newbyte;
		}
		thiscode = (thiscode<<1) + tempcode;
		codelen++;
		if(codelen > 16)
			return FUNC_FORMAT_ERROR;
	}  //while
	
	temp = thiscode - huf_min_value[HufTabIndex][codelen-1] + code_pos_table[HufTabIndex][codelen-1];
	hufexbyte = (unsigned char)code_value_table[HufTabIndex][temp];
	rrun = (short)(hufexbyte>>4);
	runsize = hufexbyte & 0x0f;
	if(runsize == 0)
	{
		vvalue = 0;
		return FUNC_OK;
	}
	tempsize = runsize;
	
	if(BitPos >= runsize)
	{
		BitPos -= runsize;
		valueex = (unsigned char)CurByte>>BitPos;
		CurByte = CurByte & And[BitPos];
	}
	else
	{
		valueex = CurByte;
		tempsize -= BitPos;
		while(tempsize > 8)
		{
			lastbyte = ReadByte();
			valueex = (valueex<<8) + (unsigned char)lastbyte;
			tempsize -= 8;
		}  //while
		lastbyte = ReadByte();
		BitPos -= tempsize;
		valueex = (valueex<<tempsize) + (lastbyte>>BitPos);
		CurByte = lastbyte & And[BitPos];
	}  //else
	
	sign = valueex >> (runsize-1);
	
	if(sign)
		vvalue = valueex;
	else
	{
		valueex = valueex ^ 0xffff;
		temp = 0xffff << runsize;
		vvalue = -(short)(valueex^temp);
	}
	
	return FUNC_OK;
}


static int HufBlock(unsigned char dchufindex, unsigned char achufindex)
{
	short count = 0;
	short i;
	int funcret;
	
	//dc
	HufTabIndex = dchufindex;
	funcret = DecodeElement();
	if(funcret != FUNC_OK)
		return funcret;
	
	BlockBuffer[count++] = vvalue;
	//ac
	HufTabIndex = achufindex;
	while(count < 64)
	{
		funcret = DecodeElement();
		if(funcret != FUNC_OK)
			return funcret;
		if((rrun == 0) && (vvalue == 0))
		{
			for(i=count; i<64; i++)
				BlockBuffer[i] = 0;
			count = 64;
		}
		else
		{
			for(i=0; i<rrun; i++)
				BlockBuffer[count++] = 0;
			BlockBuffer[count++] = vvalue;
		}
	}
	
	return FUNC_OK;
}


static void IQtIZzMCUComponent(short flag)
{
	short H, VV;
	short i, j;
	int *pQtZzMCUBuffer;
	short  *pMCUBuffer;

	switch(flag)
	{
	case 0:
		H = SampRate_Y_H;
		VV = SampRate_Y_V;
		pMCUBuffer = MCUBuffer;
		pQtZzMCUBuffer = QtZzMCUBuffer;
		break;
	case 1:
		H = SampRate_U_H;
		VV = SampRate_U_V;
		pMCUBuffer = MCUBuffer + Y_in_MCU*64;
		pQtZzMCUBuffer = QtZzMCUBuffer + Y_in_MCU*64;
		break;
	case 2:
		H = SampRate_V_H;
		VV = SampRate_V_V;
		pMCUBuffer = MCUBuffer + (Y_in_MCU+U_in_MCU)*64;
		pQtZzMCUBuffer = QtZzMCUBuffer + (Y_in_MCU+U_in_MCU)*64;
		break;
	}
	for(i=0; i<VV; i++)
		for(j=0; j<H; j++)
			IQtIZzBlock(pMCUBuffer+(i*H+j)*64, pQtZzMCUBuffer+(i*H+j)*64, flag);
}

static void IQtIZzBlock(short  *s, int *d, short flag)
{
	short i, j;
	short tag;
	short *pQt;
	int buffer2[8][8];
	int *buffer1;
	short offset;

	switch(flag)
	{
	case 0:
		pQt = YQtTable;
		offset = 128;
		break;
	case 1:
		pQt = UQtTable;
		offset = 0;
		break;
	case 2:
		pQt = VQtTable;
		offset = 0;
		break;
	}

	for(i=0; i<8; i++)
	{
		for(j=0; j<8; j++)
		{
			tag = Zig_Zag[i][j];
			buffer2[i][j] = (int)s[tag] * (int)pQt[tag];
		}
	}
	buffer1 = (int *)buffer2;
	Fast_IDCT(buffer1);
	for(i=0; i<8; i++)
		for(j=0; j<8; j++)
			d[i*8+j] = buffer2[i][j] + offset;
}

static void Fast_IDCT(int * block)
{
	short i;

	for(i=0; i<8; i++)
		idctrow(block+8*i);

	for(i=0; i<8; i++)
		idctcol(block+i);
}

static unsigned char ReadByte()
{
	unsigned char i;

	i = *(lp++);
	if(i == 0xff)
		lp++;
	BitPos = 8;
	CurByte = i;
	return i;
}

static void Initialize_Fast_IDCT()
{
	short i;

	iclp = iclip+512;
	for(i= -512; i<512; i++)
		iclp[i] = (i < -256) ? (-256) : ((i>255) ? 255 : i);
}

static void idctrow(int * blk)
{
	int x0, x1, x2, x3, x4, x5, x6, x7, x8;

	//intcut
	if(!((x1 = blk[4]<<11) | (x2 = blk[6]) | (x3 = blk[2]) |
		(x4 = blk[1]) | (x5 = blk[7]) | (x6 = blk[5]) | (x7 = blk[3])))
	{
		blk[0] = blk[1] = blk[2] = blk[3] = blk[4] = blk[5] = blk[6] = blk[7] = blk[0]<<3;
		return;
	}

	x0 = (blk[0]<<11) + 128; // for proper rounding in the fourth stage 
	//first stage
	x8 = W7*(x4+x5);
	x4 = x8 + (W1-W7)*x4;
	x5 = x8 - (W1+W7)*x5;
	x8 = W3*(x6+x7);
	x6 = x8 - (W3-W5)*x6;
	x7 = x8 - (W3+W5)*x7;
	//second stage
	x8 = x0 + x1;
	x0 -= x1;
	x1 = W6*(x3+x2);
	x2 = x1 - (W2+W6)*x2;
	x3 = x1 + (W2-W6)*x3;
	x1 = x4 + x6;
	x4 -= x6;
	x6 = x5 + x7;
	x5 -= x7;
	//third stage
	x7 = x8 + x3;
	x8 -= x3;
	x3 = x0 + x2;
	x0 -= x2;
	x2 = (181*(x4+x5)+128)>>8;
	x4 = (181*(x4-x5)+128)>>8;
	//fourth stage
	blk[0] = (x7+x1)>>8;
	blk[1] = (x3+x2)>>8;
	blk[2] = (x0+x4)>>8;
	blk[3] = (x8+x6)>>8;
	blk[4] = (x8-x6)>>8;
	blk[5] = (x0-x4)>>8;
	blk[6] = (x3-x2)>>8;
	blk[7] = (x7-x1)>>8;
}

static void idctcol(int * blk)
{
	int x0, x1, x2, x3, x4, x5, x6, x7, x8;
	//intcut
	if(!((x1 = (blk[8*4]<<8)) | (x2 = blk[8*6]) | (x3 = blk[8*2]) |
		(x4 = blk[8*1]) | (x5 = blk[8*7]) | (x6 = blk[8*5]) | (x7 = blk[8*3])))
	{
		blk[8*0] = blk[8*1] = blk[8*2] = blk[8*3] = blk[8*4] = blk[8*5]
			= blk[8*6] = blk[8*7] = iclp[(blk[8*0]+32)>>6];
		return;
	}
	x0 = (blk[8*0]<<8) + 8192;
	//first stage
	x8 = W7*(x4+x5) + 4;
	x4 = (x8+(W1-W7)*x4)>>3;
	x5 = (x8-(W1+W7)*x5)>>3;
	x8 = W3*(x6+x7) + 4;
	x6 = (x8-(W3-W5)*x6)>>3;
	x7 = (x8-(W3+W5)*x7)>>3;
	//second stage
	x8 = x0 + x1;
	x0 -= x1;
	x1 = W6*(x3+x2) + 4;
	x2 = (x1-(W2+W6)*x2)>>3;
	x3 = (x1+(W2-W6)*x3)>>3;
	x1 = x4 + x6;
	x4 -= x6;
	x6 = x5 + x7;
	x5 -= x7;
	//third stage
	x7 = x8 + x3;
	x8 -= x3;
	x3 = x0 + x2;
	x0 -= x2;
	x2 = (181*(x4+x5)+128)>>8;
	x4 = (181*(x4-x5)+128)>>8;
	//fourth stage
	blk[8*0] = iclp[(x7+x1)>>14];
	blk[8*1] = iclp[(x3+x2)>>14];
	blk[8*2] = iclp[(x0+x4)>>14];
	blk[8*3] = iclp[(x8+x6)>>14];
	blk[8*4] = iclp[(x8-x6)>>14];
	blk[8*5] = iclp[(x0-x4)>>14];
	blk[8*6] = iclp[(x3-x2)>>14];
	blk[8*7] = iclp[(x7-x1)>>14];
}

static int DecodeMCUBlock()
{
	short *lpMCUBuffer;
	short i, j;
	int funcret;
	
	if(IntervalFlag)
	{
		lp += 2;
		ycoef = ucoef = vcoef = 0;
		BitPos = 0;
		CurByte = 0;
	}

	switch(comp_num)
	{
	case 3:
		lpMCUBuffer = MCUBuffer;
		for(i=0; i<SampRate_Y_H*SampRate_Y_V; i++)  //Y
		{
			funcret = HufBlock(YDcIndex, YAcIndex);
			if(funcret != FUNC_OK)
				return funcret;
			BlockBuffer[0] = BlockBuffer[0] + ycoef;
			ycoef = BlockBuffer[0];
			for(j=0; j<64; j++)
				*lpMCUBuffer++ = BlockBuffer[j];
		}
		for(i=0; i<SampRate_U_H*SampRate_U_V; i++)  //U
		{
			funcret = HufBlock(UVDcIndex, UVAcIndex);
			if(funcret != FUNC_OK)
				return funcret;
			BlockBuffer[0] = BlockBuffer[0] + ucoef;
			ucoef = BlockBuffer[0];
			for(j=0; j<64; j++)
				*lpMCUBuffer++ = BlockBuffer[j];
		}
		for(i=0; i<SampRate_V_H*SampRate_V_V; i++)  //V
		{
			funcret = HufBlock(UVDcIndex, UVAcIndex);
			if(funcret != FUNC_OK)
				return funcret;
			BlockBuffer[0] = BlockBuffer[0] + vcoef;
			vcoef = BlockBuffer[0];
			for(j=0; j<64; j++)
				*lpMCUBuffer++ = BlockBuffer[j];
		}
		break;
	case 1:
		lpMCUBuffer = MCUBuffer;
		funcret = HufBlock(YDcIndex, YAcIndex);
		if(funcret != FUNC_OK)
			return funcret;
		BlockBuffer[0] = BlockBuffer[0] + ycoef;
		ycoef = BlockBuffer[0];
		for(j=0; j<64; j++)
			*lpMCUBuffer++ = BlockBuffer[j];
		for (i=0; i<128; i++)
			*lpMCUBuffer++ = 0;
		break;
	default:
		return FUNC_FORMAT_ERROR;
	}
	return FUNC_OK;
}

static int Decode()
{
	int funcret;
	
	Y_in_MCU = SampRate_Y_H*SampRate_Y_V;
	U_in_MCU = SampRate_U_H*SampRate_U_V;
	V_in_MCU = SampRate_V_H*SampRate_V_V;
	H_YtoU = SampRate_Y_H/SampRate_U_H;
	V_YtoU = SampRate_Y_V/SampRate_U_V;
	H_YtoV = SampRate_Y_H/SampRate_V_H;
	V_YtoV = SampRate_Y_V/SampRate_V_V;
	
	Initialize_Fast_IDCT();

	while((funcret = DecodeMCUBlock()) == FUNC_OK)
	{
		interval++;
		if((restart) && (interval % restart == 0))
			IntervalFlag = 1;
		else
			IntervalFlag = 0;
		
		IQtIZzMCUComponent(0);
		IQtIZzMCUComponent(1);
		IQtIZzMCUComponent(2);
		
		GetYUV(0);
		GetYUV(1);
		GetYUV(2);
		
		StoreBuffer();
		
		sizej += SampRate_Y_H*8;
		if(sizej >= ImgWidth)
		{
			sizej = 0;
			sizei += SampRate_Y_V*8;
		}
		
		if((sizej == 0) && (sizei >= ImgHeight))
			break;
	}
	return funcret;
}

int ConvertJpegFileToBmpFile(const char *jpgname, const char *bmpname)
{
	BITMAPFILEHEADER   bf;
	BITMAPINFOHEADER   bi;
	HGLOBAL            hImgData = NULL;
	DWORD              NumColors;
	
	HFILE			   hfjpg;
	DWORD 		       ImgSize;
	DWORD              BufSize, JpegBufSize;
	HFILE              hfbmp;
	HGLOBAL			   hJpegBuf;
	int				   funcret;
	LPBITMAPINFOHEADER lpImgData;

	if((hfjpg = _lopen(jpgname, OF_READ)) == HFILE_ERROR)
		return -1;
	
	//get jpg file length
	JpegBufSize = _llseek(hfjpg, 0L, SEEK_END);
	//rewind to the beginning of the file
	_llseek(hfjpg, 0L, SEEK_SET);

	if((hJpegBuf = GlobalAlloc(GHND,JpegBufSize)) == NULL)
	{
		_lclose(hfjpg);
		return -1;
	}
	lpJpegBuf = (unsigned char  *)GlobalLock(hJpegBuf);
	_hread(hfjpg, (unsigned char  *)lpJpegBuf, JpegBufSize);
	_lclose(hfjpg);
	
	InitTable();

	if((funcret = InitTag()) != FUNC_OK)
	{
		GlobalUnlock(hJpegBuf);
		GlobalFree(hJpegBuf);
		return -1;
	}

	//create new bitmapfileheader and bitmapinfoheader
	memset((char *)&bf, 0, sizeof(BITMAPFILEHEADER));	
	memset((char *)&bi, 0, sizeof(BITMAPINFOHEADER));

	bi.biSize = (DWORD)sizeof(BITMAPINFOHEADER);
	bi.biWidth = (LONG)(ImgWidth);
	bi.biHeight = (LONG)(ImgHeight);
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biClrUsed = 0;
	bi.biClrImportant = 0;
	bi.biCompression = BI_RGB;
	NumColors = 0;
	LineBytes = (DWORD)WIDTHBYTES(bi.biWidth*bi.biBitCount);
	ImgSize = (DWORD)LineBytes*bi.biHeight;

	bf.bfType = 0x4d42;
	bf.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + NumColors*sizeof(RGBQUAD)+ImgSize;
	bf.bfOffBits = (DWORD)(NumColors*sizeof(RGBQUAD) + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
	BufSize = bf.bfSize - sizeof(BITMAPFILEHEADER);

	if((hImgData = GlobalAlloc(GHND, BufSize)) == NULL)
	{
		GlobalUnlock(hJpegBuf);
		GlobalFree(hJpegBuf);
		return -1;
	}
	lpImgData = (LPBITMAPINFOHEADER)GlobalLock(hImgData); 
	memcpy(lpImgData, (char *)&bi, sizeof(BITMAPINFOHEADER));
	lpPtr = (char *)lpImgData + sizeof(BITMAPINFOHEADER);

	if((SampRate_Y_H == 0) || (SampRate_Y_V == 0))
	{
		GlobalUnlock(hJpegBuf);
		GlobalFree(hJpegBuf);
		GlobalUnlock(hImgData);
		GlobalFree(hImgData);
		hImgData = NULL;
		return -1 ;
	}

	funcret = Decode();
	if(funcret == FUNC_OK)
	{
		hfbmp = _lcreat(bmpname, 0);
		_lwrite(hfbmp, (LPSTR)&bf, sizeof(BITMAPFILEHEADER)); 
		_lwrite(hfbmp, (LPSTR)lpImgData, BufSize);
		_lclose(hfbmp);
		
		GlobalUnlock(hJpegBuf);
		GlobalFree(hJpegBuf);
		GlobalUnlock(hImgData);
		return 0;
	}
	else
	{
		GlobalUnlock(hJpegBuf);
		GlobalFree(hJpegBuf);
		GlobalUnlock(hImgData);
		GlobalFree(hImgData);
		hImgData = NULL;
		return -1;
	}
}


void PrepareForVideoDecode(AMVInfo *info)
{
	short i, j, huftabindex;
	
	sizei = sizej = 0;
	ImgWidth = ImgHeight = 0;
	rrun = vvalue = 0;
	BitPos = 0;
	CurByte = 0;
	IntervalFlag = 0;
	restart = 0;
	//////////////////////////////////////////////////////////////////////////
	ImgWidth = info->dwWidth;
	ImgHeight = info->dwHeight;

	for(i=0; i<3; i++)
		for(j=0; j<64; j++)
			qt_table[i][j] = 0;
	//////////////////////////////////////////////////////////////////////////
	for(j=0; j<64; j++)
		qt_table[0][j] = amv_luminance_quant_tbl[j];
	for(j=0; j<64; j++)
		qt_table[1][j] = amv_chrominance_quant_tbl[j];

	comp_num = 0;
	HufTabIndex = 0;
	
	for(i=0; i<3; i++)
		comp_index[i] = 0;
	//////////////////////////////////////////////////////////////////////////
	comp_num = 3;
	comp_index[0] = 1;
	SampRate_Y_H = 2;
	SampRate_Y_V = 2;
	YQtTable = (short *)qt_table[0];
	comp_index[1] = 2;
	SampRate_U_H = 1;
	SampRate_U_V = 1;
	UQtTable = (short *)qt_table[1];
	comp_index[2] = 3;
	SampRate_V_H = 1;
	SampRate_V_V = 1;
	VQtTable = (short *)qt_table[1];

	YDcIndex = 0;		//Y
	YAcIndex = 0 + 2;
	UVDcIndex = 1;		// U,V
	UVAcIndex = 1 + 2;

	for(i=0; i<4; i++)
		for(j=0; j<16; j++)
		{
			code_len_table[i][j] = 0;
			code_pos_table[i][j] = 0;
			huf_max_value[i][j] = 0;
			huf_min_value[i][j] = 0;
		}
	//////////////////////////////////////////////////////////////////////////
	for(i=0; i<16; i++)
	{
		code_len_table[0][i] = bits_dc_luminance[i+1];
		code_len_table[1][i] = bits_dc_chrominance[i+1];
		code_len_table[2][i] = bits_ac_luminance[i+1];
		code_len_table[3][i] = bits_ac_chrominance[i+1];
	}
	for(huftabindex=0; huftabindex<4; huftabindex++)
	{
		i = 0;
		while(code_len_table[huftabindex][i] == 0)
			i++;
		for(j=0; j<i; j++)
		{
			huf_min_value[huftabindex][j] = 0;
			huf_max_value[huftabindex][j] = 0;
		}
		huf_min_value[huftabindex][i] = 0;
		huf_max_value[huftabindex][i] = code_len_table[huftabindex][i]-1;
		for(j=i+1; j<16; j++)
		{
			huf_min_value[huftabindex][j] = (huf_max_value[huftabindex][j-1]+1)<<1;
			huf_max_value[huftabindex][j] = huf_min_value[huftabindex][j] + code_len_table[huftabindex][j]-1;
		}
		code_pos_table[huftabindex][0] = 0;
		for(j=1; j<16; j++)
			code_pos_table[huftabindex][j] = code_len_table[huftabindex][j-1] + code_pos_table[huftabindex][j-1];
	}


	for(i=0; i<4; i++)
		for(j=0; j<256; j++)
			code_value_table[i][j] = 0;
	//////////////////////////////////////////////////////////////////////////
	for(i=0; i<12; i++)
	{
		code_value_table[0][i] = val_dc_luminance[i];
		code_value_table[1][i] = val_dc_chrominance[i];
	}
	for(i=0; i<162; i++)
	{
		code_value_table[2][i] = val_ac_luminance[i];
		code_value_table[3][i] = val_ac_chrominance[i];
	}
	
	for(i=0; i<10*64; i++)
	{
		MCUBuffer[i] = 0;
		QtZzMCUBuffer[i] = 0;
	}
	for(i=0; i<64; i++)
	{
		Y[i] = 0;
		U[i] = 0;
		V[i] = 0;
		BlockBuffer[i] = 0;
	}
	ycoef = ucoef = vcoef = 0;

}

int AmvJpegDecode(AMVInfo *info, FRAMEBUFF *inbuff, VIDEOBUFF *video)
{
	int funcret;

	if(info == NULL)
		return -1;

	PrepareForVideoDecode(info);

	LineBytes = (DWORD)WIDTHBYTES(ImgWidth*24);

	lpPtr = (char *)video->fbmpdat;
	lp = (inbuff->videobuff + 2);		// escape 0xff 0xd8
	
	funcret = Decode();
	
	if(funcret == FUNC_OK)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

//for C linkage
#ifdef __cplusplus
	}
#endif