#ifndef __AMVHEADER_H__
#define __AMVHEADER_H__

#include <stdio.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;
typedef DWORD          FOURCC;

#ifndef mmioFOURCC
#define mmioFOURCC( ch0, ch1, ch2, ch3 ) \
    ( (DWORD)(BYTE)(ch0) | ( (DWORD)(BYTE)(ch1) << 8 ) |    \
( (DWORD)(BYTE)(ch2) << 16 ) | ( (DWORD)(BYTE)(ch3) << 24 ) )
#endif

// AMV Data Structure - Start >>>>>>>>>>>>>>>>>>>>>>>>>>>>>
typedef struct _amv_main_header
{
	FOURCC fcc;					// 必须为‘amvh’
	DWORD size;					// 本数据结构的大小，不包括最初的8 个字节
	DWORD dwMicroSecPerFrame;	// 视频帧间隔时间（以微秒为单位）
	BYTE reserved[28];			// 初步分析，这28 个字节作为备用，全部为零。
/*	DWORD dwMaxBytesPerSec;		// 这个AVI 文件的最大数据率
	DWORD dwPaddingGranularity;	// 数据填充的粒度
	DWORD dwFlags;				// AVI文件的全局标记，比如是否含有索引块等
	DWORD dwTotalFrames;		// 总帧数
	DWORD dwInitialFrames;		// 为交互格式指定初始帧数（非交互格式应该指定为0）
	DWORD dwStreams;			// 本文件包含的流的个数
	DWORD dwSuggestedBufferSize; // 建议读取本文件的缓存大小（应能容纳最大的块）
*/	DWORD dwWidth;				// 视频图像的宽（以像素为单位）
	DWORD dwHeight;				// 视频图像的高（以像素为单位）
	DWORD dwSpeed;				// 帧速度 /（帧/秒)
	DWORD reserve0;				// 值等于1，用途还不清楚
	DWORD reserve1;				// 值等于0，用途还不清楚
	BYTE dwTimeSec;				// 总时间（秒）
	BYTE dwTimeMin;				// 总时间（分）
	WORD dwTimeHour;			// 总时间 （小时）
} AMVMainHeader;

typedef struct _amv_stream_header
{
    FOURCC fcc;					// 必须为‘strh’
    DWORD size;
	BYTE reserved[56];
/*	FOURCC fccType;				// 流的类型：‘auds’（音频流）、‘vids’（视频流）、
								//‘mids’（MIDI流）、‘txts’（文字流）
	FOURCC fccHandler;			// 指定流的处理者，对于音视频来说就是解码器
	DWORD dwFlags;				// 标记：是否允许这个流输出？调色板是否变化？
	WORD wPriority;				// 流的优先级（当有多个相同类型的流时优先级最高的为默认流）
	WORD wLanguage;
	DWORD dwInitialFrames;		// 为交互格式指定初始帧数
	DWORD dwScale;				// 这个流使用的时间尺度
	DWORD dwRate;
	DWORD dwStart;				// 流的开始时间
	DWORD dwLength;				// 流的长度（单位与dwScale和dwRate 的定义有关）
	DWORD dwSuggestedBufferSize; // 读取这个流数据建议使用的缓存大小
	DWORD dwQuality;			// 流数据的质量指标（0 ~ 10,000）
	DWORD dwSampleSize;			// Sample 的大小
	struct {
		short int left;
		short int top;
		short int right;
		short int bottom;
	} rcFrame;					// 指定这个流（视频流或文字流）在视频主窗口中的显示位置
								// 视频主窗口由AMVMAINHEADER结构中的dwWidth 和dwHeight 决定
*/
} AMVVideoStreamHeader;

typedef struct _amv_bitmap_info_header
{
    FOURCC fcc;					// 必须为‘strf’
	DWORD size;
	BYTE reserved[36];
/*	DWORD biWidth;
    DWORD biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    DWORD biXPelsPerMeter;
    DWORD biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
*/
} AMVBitmapInfoHeader;


typedef struct _amv_audio_stream_header
{
    FOURCC fcc;					// 必须为‘strh’
    DWORD size;
	BYTE reserved[48];
} AMVAudioStreamHeader;

typedef struct _amv_wave_format_ex
{
	FOURCC fcc;					// 必须为‘strf’
	DWORD size;
	WORD wFormatTag;			
	WORD nChannels;				// 声道数
	DWORD nSamplesPerSec;		// 采样率
	DWORD nAvgBytesPerSec;		// 每秒平均字节数
	WORD nBlockAlign;
	WORD wBitsPerSample;		// 采样位数
	WORD cbSize;
	WORD wSamplesPerBlock;
} AMVWaveFormatEx;


typedef struct _amv_info_struct
{
	FOURCC ccRIFF;
	DWORD riffSize;
	FOURCC riffName;
	FOURCC ccLIST;				// "LIST"
	DWORD listSize;
	FOURCC listType;			// "hdrl"
	AMVMainHeader main_header;	// listData
	
	//////////////// video header <start> ///////////////
	FOURCC ccLISTV;				// "LIST"
	DWORD listSizeV;
	FOURCC listTypeVStrl;		// "strl"
	AMVVideoStreamHeader vstream_header;	// listData
	AMVBitmapInfoHeader vinfo_header;	// listData
	//////////////// video header <end> /////////////////

	//////////////// audio header <start> ///////////////
	FOURCC ccLISTA;				// "LIST"
	DWORD listSizeA;
	FOURCC listTypeAStrl;		// "strl"
	AMVAudioStreamHeader astream_header;	// listData
	AMVWaveFormatEx ainfo_header;	// listData
	//////////////// audio header <end> /////////////////
} AMVHeader;



#endif /* __AMVHEADER_H__ */