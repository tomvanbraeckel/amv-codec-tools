#ifndef __ADPCM_IMA_H__
#define __ADPCM_IMA_H__

//for C linkage
#ifdef __cplusplus
extern "C" {
#endif
	
#define BLKSIZE		1024

typedef struct ADPCMChannelStatus
{
	int predictor;
	short int step_index;
	int step;
	/* for encoding */
	int prev_sample;
} ADPCMChannelStatus;

typedef struct ADPCMContext
{
	int channel; /* for stereo MOVs, decode left, then decode right, then tell it's decoded */
	ADPCMChannelStatus status[2];
	short sample_buffer[32]; /* hold left samples while waiting for right samples */
} ADPCMContext;

	
int AdpcmImaEncodeFrame(ADPCMContext *c, int channels, int frame_size, 
						unsigned char *frame, int buf_size, void *data);
int AdpcmImaDecodeFrame(ADPCMContext *c,
						void *data, int *data_size,
						unsigned char *buf, int buf_size);
//for C linkage
#ifdef __cplusplus
	}
#endif


#endif /* __ADPCM_IMA_H__ */
