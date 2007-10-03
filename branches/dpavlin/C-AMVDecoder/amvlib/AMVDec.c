#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "AMVHeader.h"
#include "AMVDec.h"
#include "AdpcmIma.h"
#include "AmvJpeg.h"

//for C linkage
#ifdef __cplusplus
extern "C" {
#endif


AMVLIB_API AMVDecoder *AmvOpen(const char *amvname)
{
	FILE *fp;
	AMVHeader *amvhead;
	AMVDecoder *amv;
	FOURCC cctmp;
	unsigned int rtnlen;

	if(amvname == NULL)
		return NULL;

	amv = (AMVDecoder *)malloc(sizeof(AMVDecoder));
	if(amv == NULL)
		return NULL;
	memset((unsigned char *)amv, 0, sizeof(AMVDecoder));

	fp = fopen(amvname, "rb");
	if(!fp)
	{
		free(amv);
		return NULL;
	}

	amvhead = (AMVHeader *)malloc(sizeof(AMVHeader));
	if(amvhead == NULL)
	{
		fclose(fp);
		free(amv);
		return NULL;
	}
	memset(amvhead, 0, sizeof(AMVHeader));

	rtnlen = fread(amvhead, sizeof(char), sizeof(AMVHeader), fp);
	amv->fileseekpos += rtnlen;
	
	if(amvhead->ccRIFF != mmioFOURCC('R', 'I', 'F', 'F'))
		goto _amvhead_not_match;
	if(amvhead->riffName != mmioFOURCC('A', 'M', 'V', ' '))
		goto _amvhead_not_match;
	if(amvhead->ccLIST != mmioFOURCC('L', 'I', 'S', 'T'))
		goto _amvhead_not_match;
	if(amvhead->listType != mmioFOURCC('h', 'd', 'r', 'l'))
		goto _amvhead_not_match;
	if(amvhead->main_header.fcc != mmioFOURCC('a', 'm', 'v', 'h'))
		goto _amvhead_not_match;
	
	//////////////// video header <start> ///////////////
	if(amvhead->ccLISTV != mmioFOURCC('L', 'I', 'S', 'T'))
		goto _amvhead_not_match;
	if(amvhead->listTypeVStrl != mmioFOURCC('s', 't', 'r', 'l'))
		goto _amvhead_not_match;
	if(amvhead->vstream_header.fcc != mmioFOURCC('s', 't', 'r', 'h'))
		goto _amvhead_not_match;
	if(amvhead->vinfo_header.fcc != mmioFOURCC('s', 't', 'r', 'f'))
		goto _amvhead_not_match;
	//////////////// video header <end> /////////////////
	
	//////////////// audio header <start> ///////////////
	if(amvhead->ccLISTA != mmioFOURCC('L', 'I', 'S', 'T'))
		goto _amvhead_not_match;
	if(amvhead->listTypeAStrl != mmioFOURCC('s', 't', 'r', 'l'))
		goto _amvhead_not_match;
	if(amvhead->astream_header.fcc != mmioFOURCC('s', 't', 'r', 'h'))
		goto _amvhead_not_match;
	if(amvhead->ainfo_header.fcc != mmioFOURCC('s', 't', 'r', 'f'))
		goto _amvhead_not_match;
	//////////////// audio header <end> /////////////////

	rtnlen = fread(&cctmp, sizeof(char), 4, fp);
	amv->fileseekpos += rtnlen;
	if(cctmp != mmioFOURCC('L', 'I', 'S', 'T'))
		goto _amvhead_not_match;

	rtnlen = fread(&cctmp, sizeof(char), 4, fp);
	amv->fileseekpos += rtnlen;
	rtnlen = fread(&cctmp, sizeof(char), 4, fp);
	amv->fileseekpos += rtnlen;
	if(cctmp != mmioFOURCC('m', 'o', 'v', 'i'))
		goto _amvhead_not_match;
	
	amv->amvinfo.dwMicroSecPerFrame = amvhead->main_header.dwMicroSecPerFrame;
	amv->amvinfo.dwWidth = amvhead->main_header.dwWidth;
	amv->amvinfo.dwHeight = amvhead->main_header.dwHeight;
	amv->amvinfo.dwSpeed = amvhead->main_header.dwSpeed;
	amv->amvinfo.dwTimeHour = amvhead->main_header.dwTimeHour;
	amv->amvinfo.dwTimeMin = amvhead->main_header.dwTimeMin;
	amv->amvinfo.dwTimeSec = amvhead->main_header.dwTimeSec;
	
	amv->amvinfo.wFormatTag = amvhead->ainfo_header.wFormatTag;
	amv->amvinfo.nChannels = amvhead->ainfo_header.nChannels;
	amv->amvinfo.nSamplesPerSec = amvhead->ainfo_header.nSamplesPerSec;
	amv->amvinfo.wBitsPerSample = amvhead->ainfo_header.wBitsPerSample;
	amv->amvinfo.nAvgBytesPerSec = amvhead->ainfo_header.nAvgBytesPerSec;
	amv->amvinfo.nBlockAlign = amvhead->ainfo_header.nBlockAlign;
	amv->amvinfo.cbSize = amvhead->ainfo_header.cbSize;
	amv->amvinfo.wSamplesPerBlock = amvhead->ainfo_header.wSamplesPerBlock;

	amv->totalframe = (amv->amvinfo.dwTimeHour * 60 *60 +
						amv->amvinfo.dwTimeMin * 60 +
						amv->amvinfo.dwTimeSec) * amv->amvinfo.dwSpeed;
	amv->amvfilename = strdup(amvname);
	amv->opened = 1;
	amv->dataseekpos = amv->fileseekpos;

	fclose(fp);
	free(amvhead);
	
	return amv;

_amvhead_not_match:
	fclose(fp);
	free(amv);
	free(amvhead);
	return NULL;
}

AMVLIB_API void AmvClose(AMVDecoder *amv)
{
	if(amv == NULL)
		return;

	if(amv->amvfilename)
		free(amv->amvfilename);
	if(amv->framebuf.audiobuff)
		free(amv->framebuf.audiobuff);
	if(amv->framebuf.videobuff)
		free(amv->framebuf.videobuff);
	if(amv->videobuf.fbmpdat)
		free(amv->videobuf.fbmpdat);
	if(amv->audiobuf.audiodata)
		free(amv->audiobuf.audiodata);

	free(amv);
}

AMVLIB_API int AmvReadNextFrame(AMVDecoder *amv)
{
	FILE *fp;
	FOURCC cctmp;
	unsigned long len, rtnlen;
	FRAMEBUFF *fbuff;
	
	if(amv == NULL)
		return -1;
	if(!amv->opened || amv->amvfilename == NULL)
		return -1;
	
	fbuff = &amv->framebuf;

	fp = fopen(amv->amvfilename, "rb");
	if(fp == NULL)
		return -1;
	fseek(fp, amv->fileseekpos, SEEK_SET);

	rtnlen = fread(&cctmp, sizeof(char), 4, fp);
	amv->fileseekpos += rtnlen;
	if(cctmp != mmioFOURCC('0', '0', 'd', 'c'))
	{
		if(cctmp == mmioFOURCC('A', 'M', 'V', '_'))
		{
			rtnlen = fread(&cctmp, sizeof(char), 4, fp);
			amv->fileseekpos += rtnlen;
			if(cctmp == mmioFOURCC('E', 'N', 'D', '_'))
			{
				if(fbuff->videobuff)
					free(fbuff->videobuff);
				fbuff->videobuff = NULL;
				if(fbuff->audiobuff)
					free(fbuff->audiobuff);
				fbuff->audiobuff = NULL;
				fbuff->videobufflen = 0;
				fbuff->audiobufflen = 0;
				fbuff->framenum = -1;
				fclose(fp);
				return 0;
			}
		}
		fclose(fp);
		return -1;
	}
	
	rtnlen = fread(&len, sizeof(char), 4, fp);			// 视频数据长度
	amv->fileseekpos += rtnlen;
	if(fbuff->videobuff)
		free(fbuff->videobuff);
	fbuff->videobuff = (unsigned char *)malloc(len);
	if(fbuff->videobuff == NULL)
	{
		fclose(fp);
		return -1;
	}
	rtnlen = fread(fbuff->videobuff, sizeof(char), len, fp);
	amv->fileseekpos += rtnlen;
	fbuff->videobufflen = len;

	
	rtnlen = fread(&cctmp, sizeof(char), 4, fp);
	amv->fileseekpos += rtnlen;
	if(cctmp != mmioFOURCC('0', '1', 'w', 'b'))
	{
		fclose(fp);
		return -1;
	}
	rtnlen = fread(&len, sizeof(char), 4, fp);			// 音频数据长度
	amv->fileseekpos += rtnlen;
	
	if(fbuff->audiobuff)
		free(fbuff->audiobuff);
	fbuff->audiobuff = (unsigned char *)malloc(len);
	if(fbuff->audiobuff == NULL)
	{
		fclose(fp);
		return -1;
	}
	rtnlen = fread(fbuff->audiobuff, sizeof(char), len, fp);
	amv->fileseekpos += rtnlen;
	fbuff->audiobufflen = len;
	
	fbuff->framenum++;
	amv->currentframe = fbuff->framenum;
	
	fclose(fp);
	return 0;
}

AMVLIB_API int AmvRewindFrameStart(AMVDecoder *amv)
{
	FILE *fp;

	if(amv == NULL)
		return -1;
	if(!amv->opened || amv->amvfilename == NULL)
		return -1;
		
	fp = fopen(amv->amvfilename, "rb");
	if(fp == NULL)
		return -1;
	fseek(fp, amv->dataseekpos, SEEK_SET);
	amv->fileseekpos = amv->dataseekpos;

	fclose(fp);
	return 0;
}

AMVLIB_API int AmvVideoDecode(AMVDecoder *amv)
{
	AMVInfo *amvinfo;
	FRAMEBUFF *fbuff;
	VIDEOBUFF *vbuff;

	if(amv == NULL)
		return -1;
	if(!amv->opened)
		return -1;

	fbuff = &(amv->framebuf);
	if(fbuff->videobuff == NULL || fbuff->videobufflen == 0)
		return -1;

	amvinfo = &(amv->amvinfo);
	vbuff = &(amv->videobuf);

	vbuff->len = amvinfo->dwHeight * amvinfo->dwWidth * 3;
	if(vbuff->fbmpdat)
		free(vbuff->fbmpdat);
	vbuff->fbmpdat = (unsigned char *)malloc(vbuff->len);
	if(vbuff->fbmpdat == NULL)
		return -2;
	memset(vbuff->fbmpdat, 0, vbuff->len);
	
	return AmvJpegDecode(amvinfo, fbuff, vbuff);
}

AMVLIB_API int AmvAudioDecode(AMVDecoder *amv)
{
	int rtn, declen;
	AMVInfo *amvinfo;
	FRAMEBUFF *fbuff;
	AUDIOBUFF *abuff;
	ADPCMContext audio;

	if(amv == NULL)
		return -1;
	if(!amv->opened)
		return -1;
	
	fbuff = &(amv->framebuf);
	if(fbuff->audiobuff == NULL || fbuff->audiobufflen == 0)
		return -1;
	
	amvinfo = &(amv->amvinfo);
	abuff = &(amv->audiobuf);

	memset((unsigned char *)&audio, 0, sizeof(ADPCMContext));
	audio.channel = amvinfo->nChannels;

//	audio.status[0].predictor = fbuff->audiobuff[0] + (fbuff->audiobuff[1]<<8);
	audio.status[0].predictor = *((short*)(fbuff->audiobuff));//此处应为short型
	audio.status[0].step_index = fbuff->audiobuff[2];
//	audio.status[0].predictor = 0;
//	audio.status[0].step_index = 0;
	audio.status[1].predictor = audio.status[0].predictor;
	audio.status[1].step_index = audio.status[0].step_index;
	
	abuff->len = fbuff->audiobuff[4] + (fbuff->audiobuff[5]<<8) +
					(fbuff->audiobuff[6]<<16) + (fbuff->audiobuff[7]<<24);
	abuff->len *= 2;
	if(abuff->len < (fbuff->audiobufflen-8)*4)
		abuff->len = (fbuff->audiobufflen-8)*4;
	if(abuff->audiodata)
		free(abuff->audiodata);
	abuff->audiodata = (short *)malloc(abuff->len + 16);
	if(abuff->audiodata == NULL)
		return -2;
	memset((unsigned char *)abuff->audiodata, 0, abuff->len);
	
	rtn = AdpcmImaDecodeFrame(&audio, abuff->audiodata, &declen, 
								fbuff->audiobuff+8, fbuff->audiobufflen-8);
	if(rtn > 0)
	{
		abuff->len = declen;
		return 0;
	}

	return rtn;
}

AMVLIB_API int AmvCreateJpegFileFromFrameBuffer(AMVDecoder *amv, const char *dirname)
{
	FILE *wrfp;
	char wrfname[128];
	
	sprintf(wrfname, "%s-amvjpg_%06d_.jpg", dirname, amv->framebuf.framenum);
	wrfp = fopen(wrfname, "wb");
	AmvJpegPutHeader(wrfp, (unsigned short)amv->amvinfo.dwHeight, 
								(unsigned short)amv->amvinfo.dwWidth);
	
	fwrite((amv->framebuf.videobuff+2), amv->framebuf.videobufflen-2, 1, wrfp);
	
	fclose(wrfp);
	return 0;
}

AMVLIB_API int AmvCreateJpegFileFromBuffer(AMVInfo *amvinfo, 
											FRAMEBUFF *framebuf, 
											const char *filename)
{
	FILE *wrfp;
	
	wrfp = fopen(filename, "wb");
	if(wrfp == NULL)
		return -1;
	AmvJpegPutHeader(wrfp, (unsigned short)amvinfo->dwHeight, 
								(unsigned short)amvinfo->dwWidth);
	
	fwrite((framebuf->videobuff+2), framebuf->videobufflen-2, 1, wrfp);
	
	fclose(wrfp);
	return 0;
}

AMVLIB_API int AmvConvertJpegFileToBmpFile(const char *jpgname, const char *bmpname)
{
	if(jpgname == NULL || bmpname == NULL)
		return -1;
	
	return ConvertJpegFileToBmpFile(jpgname, bmpname);
}

AMVLIB_API int AmvCreateWavFileFromAmvFile(AMVDecoder *amv, int type, const char *wavfile)
{
	static const unsigned char adpcminfo[] = 
	{
		0xF9, 0x03,	0x66, 0x61, 0x63, 0x74, 0x04, 0x00, 
		0x00, 0x00, 0x3D, 0x4C, 0x00, 0x00
	};

	int retval, first = 0;
	unsigned int pcmlen, totlen;
	unsigned short stmp;
	unsigned char pre_index[4];
	
	long dataseekpos_save;
	long fileseekpos_save;

	AMVInfo *info;
	FRAMEBUFF *fbuff;
	AUDIOBUFF *abuff;
	
	FILE *m_file;
	unsigned int m_WaveHeaderSize = 38;
	unsigned int m_WaveFormatSize = 18;
	
	if(amv == NULL)
		return -1;
	if(!amv->opened)
		return -1;
	if(!(type == AUDIO_FILE_TYPE_PCM || type == AUDIO_FILE_TYPE_ADPCM_IMA))
		return -1;

	dataseekpos_save = amv->dataseekpos;
	fileseekpos_save = amv->fileseekpos;

	totlen = 0;
	m_file = fopen(wavfile, "wb");
	if(m_file == NULL)
		return -1;
	
	info = &(amv->amvinfo);

	fwrite("RIFF", 1, 4, m_file);
	pcmlen = m_WaveHeaderSize;
	fwrite(&pcmlen, 1, 4, m_file);
	fwrite("WAVE", 1, 4, m_file);
	fwrite("fmt ", 1, 4, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		m_WaveFormatSize = 0x14;
	else	// AUDIO_FILE_TYPE_PCM
		m_WaveFormatSize = 18;
	fwrite(&m_WaveFormatSize, 1, 4, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		stmp = 0x11;
	else	// AUDIO_FILE_TYPE_PCM
		stmp = info->wFormatTag;
	fwrite(&stmp, 1, 2, m_file);
	
	stmp = info->nChannels;
	fwrite(&stmp, 1, 2, m_file);
	
	pcmlen = info->nSamplesPerSec;
	fwrite(&pcmlen, 1, 4, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		pcmlen = info->nAvgBytesPerSec/4;
	else	// AUDIO_FILE_TYPE_PCM
		pcmlen = info->nAvgBytesPerSec;
	fwrite(&pcmlen, 1, 4, m_file);
	
	stmp = info->nBlockAlign;
	fwrite(&stmp, 1, 2, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		stmp = info->wBitsPerSample/4;
	else	// AUDIO_FILE_TYPE_PCM
		stmp = info->wBitsPerSample;
	fwrite(&stmp, 1, 2, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		stmp = 2;
	else	// AUDIO_FILE_TYPE_PCM
		stmp = info->cbSize;
	fwrite(&stmp, 1, 2, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
	{
		fwrite(adpcminfo, 1, 2, m_file);
	}
	
	fwrite("data", 1, 4, m_file);
	pcmlen = 0;
	fwrite(&pcmlen, 1, 4, m_file);
	
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
	{
		pcmlen = 0;
		fwrite(&pcmlen, 1, 4, m_file);
	}

	while(1)
	{
		retval = AmvReadNextFrame(amv);
		if(retval)
			break;
		fbuff = &(amv->framebuf);
		if(fbuff->framenum == -1)
			break;

		if(first == 0 && type == AUDIO_FILE_TYPE_ADPCM_IMA)
		{
			pre_index[0] = fbuff->audiobuff[0]; 
			pre_index[1] = fbuff->audiobuff[1];
			pre_index[2] = fbuff->audiobuff[2]; 
			pre_index[3] = fbuff->audiobuff[3];
			first = 1;
		}

		if(type == AUDIO_FILE_TYPE_PCM)
		{
			if(AmvAudioDecode(amv) == 0)
			{
				abuff = &(amv->audiobuf);
				fwrite(abuff->audiodata, 1, abuff->len, m_file);
				totlen += abuff->len;
			}
		}
		else if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		{
			fwrite(fbuff->audiobuff+8, 1, fbuff->audiobufflen-8, m_file);
			totlen += (fbuff->audiobufflen-8);
		}
	}
	
	fseek(m_file, 4, SEEK_SET);
	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
	{
		if(totlen%2)
			totlen -= 1;
		pcmlen = totlen + 0x28;
	}
	else
		pcmlen = totlen + m_WaveHeaderSize;
	fwrite(&pcmlen, 1, 4, m_file);
	
	if(type == AUDIO_FILE_TYPE_PCM)
		fseek(m_file, 42, SEEK_SET);
	else if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
		fseek(m_file, 0x2C, SEEK_SET);
	fwrite(&totlen, 1, 4, m_file);

	if(type == AUDIO_FILE_TYPE_ADPCM_IMA)
	{
		fwrite(pre_index, 1, 4, m_file);
	}

	fclose(m_file);

	amv->dataseekpos = dataseekpos_save;
	amv->fileseekpos = fileseekpos_save;
	
	return 0;
}

//for C linkage
#ifdef __cplusplus
	}
#endif
