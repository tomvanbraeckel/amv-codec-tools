#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AdpcmIma.h"


//for C linkage
#ifdef __cplusplus
extern "C" {
#endif

#define CLAMP_TO_SHORT(value)		\
		if (value > 32767)			\
			value = 32767;			\
		else if (value < -32768)	\
			value = -32768;			\

/* step_table[] and index_table[] are from the ADPCM reference source */
/* This is the index table: */
const int index_table[] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/**
 * This is the step table. Note that many programs use slight deviations from
 * this table, but such deviations are negligible:
 */
static const int step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};



static unsigned char AdpcmImaCompressSample(ADPCMChannelStatus *c, short sample)
{
	int step_index;
	unsigned char nibble;
	
	int sign = 0; /* sign bit of the nibble (MSB) */
	int delta, predicted_delta;
	
	delta = sample - c->prev_sample;
	
	if(delta < 0)
	{
		sign = 1;
		delta = -delta;
	}
	
	step_index = c->step_index;
	
	/* nibble = 4 * delta / step_table[step_index]; */
	nibble = (delta << 2) / step_table[step_index];
	
	if(nibble > 7)
		nibble = 7;
	
	step_index += index_table[nibble];
	if(step_index < 0)
		step_index = 0;
	if(step_index > 88)
		step_index = 88;
	
	/* what the decoder will find */
	predicted_delta = ((step_table[step_index] * nibble) / 4) + (step_table[step_index] / 8);
	
	if(sign)
		c->prev_sample -= predicted_delta;
	else
		c->prev_sample += predicted_delta;
	
	CLAMP_TO_SHORT(c->prev_sample);
	
	nibble += sign << 3; /* sign * 8 */
	
	/* save back */
	c->step_index = step_index;
	
	return nibble;
}


int AdpcmImaEncodeFrame(ADPCMContext *c, int channels, int frame_size, 
							unsigned char *frame, int buf_size, void *data)
{
    int n, st;
    short *samples;
    unsigned char *dst;

	dst = frame;
	samples = (short *)data;
	st = 0;						// 单声道
	/* n = (BLKSIZE - 4 * avctx->channels) / (2 * 8 * avctx->channels); */

	// case CODEC_ID_ADPCM_IMA_WAV:
		n = frame_size / 8;
		c->status[0].prev_sample = (signed short)samples[0]; /* XXX */
		/* c->status[0].step_index = 0; *//* XXX: not sure how to init the state machine */
		*dst++ = (c->status[0].prev_sample) & 0xFF; /* little endian */
		*dst++ = (c->status[0].prev_sample >> 8) & 0xFF;
		*dst++ = (unsigned char)c->status[0].step_index;
		*dst++ = 0; /* unknown */
		samples++;
		if(channels == 2)
		{
			c->status[1].prev_sample = (signed short)samples[1];
			/* c->status[1].step_index = 0; */
			*dst++ = (c->status[1].prev_sample) & 0xFF;
			*dst++ = (c->status[1].prev_sample >> 8) & 0xFF;
			*dst++ = (unsigned char)c->status[1].step_index;
			*dst++ = 0;
			samples++;
		}

		/* stereo: 4 bytes (8 samples) for left, 4 bytes for right, 4 bytes left, ... */
		for(; n>0; n--)
		{
			*dst = AdpcmImaCompressSample(&c->status[0], samples[0]) & 0x0F;
			*dst |= (AdpcmImaCompressSample(&c->status[0], samples[channels]) << 4) & 0xF0;
			dst++;
			*dst = AdpcmImaCompressSample(&c->status[0], samples[channels * 2]) & 0x0F;
			*dst |= (AdpcmImaCompressSample(&c->status[0], samples[channels * 3]) << 4) & 0xF0;
			dst++;
			*dst = AdpcmImaCompressSample(&c->status[0], samples[channels * 4]) & 0x0F;
			*dst |= (AdpcmImaCompressSample(&c->status[0], samples[channels * 5]) << 4) & 0xF0;
			dst++;
			*dst = AdpcmImaCompressSample(&c->status[0], samples[channels * 6]) & 0x0F;
			*dst |= (AdpcmImaCompressSample(&c->status[0], samples[channels * 7]) << 4) & 0xF0;
			dst++;
			/* right channel */
			if(channels == 2)
			{
				*dst = AdpcmImaCompressSample(&c->status[1], samples[1]);
				*dst |= AdpcmImaCompressSample(&c->status[1], samples[3]) << 4;
				dst++;
				*dst = AdpcmImaCompressSample(&c->status[1], samples[5]);
				*dst |= AdpcmImaCompressSample(&c->status[1], samples[7]) << 4;
				dst++;
				*dst = AdpcmImaCompressSample(&c->status[1], samples[9]);
				*dst |= AdpcmImaCompressSample(&c->status[1], samples[11]) << 4;
				dst++;
				*dst = AdpcmImaCompressSample(&c->status[1], samples[13]);
				*dst |= AdpcmImaCompressSample(&c->status[1], samples[15]) << 4;
				dst++;
			}
			samples += 8 * channels;
		}
	// break;

    return dst - frame;
}

static int AdpcmDecodeInit(ADPCMContext *c)
{
    c->channel = 0;
    c->status[0].predictor = c->status[1].predictor = 0;
    c->status[0].step_index = c->status[1].step_index = 0;
    c->status[0].step = c->status[1].step = 0;
}

static short AdpcmImaExpandNibble(ADPCMChannelStatus *c, char nibble, int shift)
{
	int step_index;
	int predictor;
	int sign, delta, diff, step;
	
	sign = nibble & 8;
	delta = nibble & 7;

	step = step_table[c->step_index];
	step_index = c->step_index + index_table[(unsigned)nibble];
    
	if(step_index < 0)
		step_index = 0;
	else if(step_index > 88)
		step_index = 88;
    
	/* perform direct multiplication instead of series of jumps proposed by
	* the reference ADPCM implementation since modern CPUs can do the mults
	* quickly enough */
	diff = ((2 * delta + 1) * step) >> shift;
	predictor = c->predictor;
    
	if(sign)
		predictor -= diff;
	else
		predictor += diff;
	
	CLAMP_TO_SHORT(predictor);
    
	c->predictor = predictor;
    c->step_index = step_index;
	
    return (short)predictor;
}

int AdpcmImaDecodeFrame(ADPCMContext *c,
						void *data, int *data_size,
						unsigned char *buf, int buf_size)
{
	int i;
	int m;
	short *samples;
	unsigned char *src;
	int st; /* stereo */


	if(data == NULL || !buf_size)
        return -1;

	samples = data;
	src = buf;

	st = (c->channel == 2 ? 1 : 0);

	while(src < buf+buf_size)
	{
		for(m=0; m<4; m++)
		{
			//压缩后的样品是按照左->右的顺序存储的吧，先高4bit后低4bit.
			for(i=0; i<=st; i++)
				*samples++ = AdpcmImaExpandNibble(&(c->status[i]), (char)(src[4*i]>>4), 3);
			for(i=0; i<=st; i++)
				*samples++ = AdpcmImaExpandNibble(&(c->status[i]), (char)(src[4*i]&0x0F), 3);
			src++;
		}
		src += 4*st;
    }

    *data_size = (unsigned char *)samples - (unsigned char *)data;

	return (src - buf);
}




//for C linkage
#ifdef __cplusplus
	}
#endif