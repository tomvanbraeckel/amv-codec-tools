// AmvLibTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conio.h"
#include "../AmvLib/AMVDec.h"
#include "../AmvLib/AMVHeader.h"

int main(int argc, char* argv[])
{
	int retval;

	AMVDecoder *amvdec;
	AMVInfo *amvinfo;
	FRAMEBUFF *fbuff;

	short prev_sample;
	unsigned char step_index;
	unsigned int pcmlen;
	AUDIOBUFF *abuff;
	
//	AmvConvertJpegFileToBmpFile("red_128_96.JPG", "red_128_96.bmp");
/*
	if(argc == 1 && argv[1] == NULL)
	{
		printf("Usage: amvlibtest ***.amv\r\n");
		return -1;
	}

	if(AMVOpen(argv[1]))
		return -1;
*/
	amvdec = AmvOpen("AMV1.amv");
	if(amvdec == NULL)
		return -1;
	amvinfo = &(amvdec->amvinfo);
	
	AmvCreateWavFileFromAmvFile(amvdec, AUDIO_FILE_TYPE_ADPCM_IMA, "AMV1.wav");

	printf("视频帧间隔时间: %d uS\r\n", amvinfo->dwMicroSecPerFrame);
	printf("视频图像宽: %d 像素\r\n", amvinfo->dwWidth);
	printf("视频图像高: %d 像素\r\n", amvinfo->dwHeight);
	printf("视频帧速度: %d 帧/秒\r\n", amvinfo->dwSpeed);
	printf("视频播放时间: %d 时 %d 分 %d 秒\r\n", amvinfo->dwTimeHour,
				amvinfo->dwTimeMin, amvinfo->dwTimeSec);
	printf("视频总帧数: %d\r\n", amvdec->totalframe);
	printf("\r\n");
	
	printf("音频通道数: %d \r\n", amvinfo->nChannels);
	printf("音频采样率: %d \r\n", amvinfo->nSamplesPerSec);
	printf("音频采样位数: %d \r\n", amvinfo->wBitsPerSample);
	printf("音频平均每秒数据: %d \r\n", amvinfo->nAvgBytesPerSec);
	
	printf("\r\n");
	while(1)
	{
		retval = AmvReadNextFrame(amvdec);
		if(retval)
			break;
		fbuff = &(amvdec->framebuf);
		if(fbuff->framenum == -1)
			break;

		printf("帧 %d :\r\n", fbuff->framenum);
		printf("\t视频数据长度: %d\r\n", fbuff->videobufflen);
		printf("\t音频数据长度: %d\r\n", fbuff->audiobufflen);
		
//		AmvCreateJpegFileFromFrameBuffer(amvdec, "d:\\");
//		AmvVideoDecode(amvdec);
		
//		AmvAudioDecode(amvdec);
		abuff = &(amvdec->audiobuf);

		prev_sample = fbuff->audiobuff[0] + (fbuff->audiobuff[1]<<8);
		step_index = fbuff->audiobuff[2];
		pcmlen = fbuff->audiobuff[4] + (fbuff->audiobuff[5]<<8) +
					(fbuff->audiobuff[6]<<16) + (fbuff->audiobuff[7]<<24);
		printf("初始值: %d, 索引值: %d, PCM 数据长度: %d, 剩余数据长度: %d\r\n",
				prev_sample, step_index, pcmlen, fbuff->audiobufflen-8);
	}
	
	AmvClose(amvdec);
	
	return 0;
}

