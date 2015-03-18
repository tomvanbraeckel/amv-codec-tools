# AMV video origin and history #

The AMV is a proprietary file format ([container format](http://en.wikipedia.org/wiki/Container_format)) created for the S1MP3 players to allow
video playback without having to rise the costs of manufacturing. The video
playback could have been another, like AVI for example, but often these formats
need special hardware codecs and they have expensive license royalties. Also,
the AMV format uses less compression and in this way there is no need for
high power processors. This also leads to higher playback time.

# The compression #
When playing AMV format, there is clearly some image de/compression taking place,
as can be demonstrated by a simple calculation (AMV video format). The image
compression is, however, low by modern standards (around 4 pixels/byte, compared
with over 10 pixels/byte for MPEG2/DVD). With a resolution of 128 × 96, and a
frame rate of 12 fps, a 30 minute file will be around 80MBytes in size.

# The AMV file format #

## Container ##
The AMV format is in fact a modified version of the AVI container, the former
Microsoft video format (now replaced by the more capable WMV). Like the AVI format,
the header contains two strings, "avih" and "amvh" ("AVI" and "avih" for AVI).
The section where codec, frame size, and other information would normally be found
is filled with zeroes (because most of that information is hard-coded into
the player's firmware). Tthe string "AMV\_END" is found at the end of the file.
The modified avi header contains several 'garbage' values because many of the parameters are hardcoded. It contains a [RIFF](http://wiki.multimedia.cx/index.php?title=RIFF) packed stream of video data and ADPCM encoded
audio. There is an [open source decoder](http://svn.rot13.org/index.cgi/amv) (written in Perl) for the format.

## Video ##
According to [its author](http://blog.rot13.org/2007/08/amv_free_decoder_works.html),
the video codec is a modified version of [Motion JPEG](http://wiki.multimedia.cx/index.php?title=Motion_JPEG)
but missing quantization tables which are fixed (from jpeg standard).

## Audio ##
Audio is a variant of IMA ADPCM where the first 8 bytes of each frame are:

  * origin (16bits)
  * index (16bits)
  * number of encoded 16-bit samples (32bits).

There is also apologize that [trellis quantizaton](http://en.wikipedia.org/wiki/Trellis_quantization) is used.

## Formal AMV movie format Specification ##

```
RIFF('AVI '
LIST(
'hdrl'
'avih'
LIST(
'strl'(video stream info)
'strh'
'strf'
)
LIST(
'strl'(audio stream info)
'strh'
'strf' )
LIST(
'movi'(data)
.
00dc(video frame data)
01wb(audio data)
00dc(video frame data)
01wb(audio data)
...
00dc(video frame data)
01wb(audio data)
'AMV_END_'
)

typedef struct _amvmainheader {
FOURCC fcc; // 'amvh'
DWORD cb;
DWORD dwMicroSecPerFrame;
BYTE reserve[28];
DWORD dwWidth;
DWORD dwHeight;
DWORD dwSpeed;
DWORD reserve0;
DWORD reserve1;
BYTE bTimeSec;
BYTE bTimeMin;
WORD wTimeHour;
} AMVMAINHEADER;

typedef struct _amvstreamheader {
FOURCC fcc; // 'strh'
DWORD cb;
BYTE reserve[56];
} AMVSTREAMHEADER;

typedef struct {
WORD wFormatTag;       //(Fixme: this is equal to PCM's 0x01 format code)
WORD nChannels;        //(Fixme: this is always 1)
DWORD nSamplesPerSec;  //(Fixme: for all known sample files this is equal to 22050)
DWORD nAvgBytesPerSec; //(Fixme: for all known sample files this is equal to 44100)
WORD nBlockAlign;      //(Fixme: this seems to be 2 in AMV files, is this correct ?)
WORD wBitsPerSample;   //(Fixme: this seems to be 16 in AMV files instead of the expected 4)
WORD cbSize;           //(Fixme: this seems to be 0 in AMV files)
WORD reserved;
} WAVEFORMATEX;
```

  * Video format is jpeg (without DQT,SOF,SOS)
  * Audio format is IMA-ADPCM
# Open source decoder #
The decoder extracts each frame into a JPEG file and then uses ffmpeg to encode the
video in another format. Here is a simple test to understand what's in the AMV video
file.

  1. make an AVI video of total 10 frames using RAW DATA format, at 12fps sized 160x120 without sound
  1. save eche frame to BMP (do not use jpeg now, name them as 01.bmp ~ 10.bmp)
  1. convert the BMP files to JPEG, be careful with the JPEG compress level
  1. convert the AVI file to 160x120 pixels, 12 fps AMV, again be careful about the Image quality setting which is the JPEG compress level, and set Image Zoom to zero
  1. compare the AMV and JPEG files using a hex editor

You will find jpeg headers inside the AMV file. According to a comment in a [blog post](http://tranquillity.multimedia.cx/?p=6),
AMV and MTV are both AVI based (ffmpeg demuxer has proven to be able to handle it,
with a few hacks), some chunks are removed, some are modified. The audio stream
is a variant of IMA ADPCM encoding. First 8 bytes of each frame are used os origin
and index (two shorts, 4 bytes) for ADPCM decoder and for length of audio frame
(long, 4 bytes). Video stream is composed of mjpeg frames (similar to jpeg images,
but without quantization tables which can be obtained in jpeg standard).


# The Apex issue #
~~There is a big possibility that the AMV movie format used by S1mp3 is a rip of 'Apex Media Video' for Nintendo Game Boy (Color). Older info on it can be found
at the [Apex Designs page](http://www.apex-designs.net/payback_gba_report6.html). Apex claim pretty good compression ratios and it is
capable of being decoded on GB. It is possible that AMV is a rip-off of that codec,
mainly considering both devices work with a Z80-based instruction set. The company
hasn't replied to the email sent about the issue.~~

AMV Video format isn't Apex Media
Video format, it is simple motion [JPEG](http://wiki.multimedia.cx/index.php?title=JPEG) where each frame is saved as [JPEG](http://wiki.multimedia.cx/index.php?title=JPEG).

# Software players that support AMV playback #
  * FFmpeg  (since SVN [r10623](https://code.google.com/p/amv-codec-tools/source/detail?r=10623))
  * MPlayer (1.0rc2 and later)
  * Proprietary converter (and the attached player) that comes on the original CD sold with the unit

# Software players that support MTV playback #
  * FFmpeg (seeking is broken).
  * Proprietary converter (and the attached player) that comes on the original CD sold with the unit.

# Hardware players that support AMV and MTV playback #
Chinese-made portable MP3 players, also known as S1 MP3 Players.

# Anime Music Video #
The AMV term can be used also to designate Anime Music Videos.

# Controversies #
The players that use this format are wrongly named "MP4 Players". In fact, the
AMV (and the MTV) formats are not belonging to the MP4 family. The proper term that
would define these players is "S1MP3" as named by their creators.

# AMV and MTV encoding/transcoding information and utilities #
[Open source AMV decoder written in Perl](http://svn.rot13.org/index.cgi/amv)

[AMV encoding tips](http://www.s1mp3.org/wiki/index.php/Video_encoding)

# External Links #
["Chinese-made portable MP3 player" article on wikipedia.org](http://en.wikipedia.org/wiki/Chinese_MP4/MTV_Player)

["S1 MP3 Player" article on wikipedia.org](http://en.wikipedia.org/wiki/S1_MP3_Player)

[www.mympxplayer.org - MP4 resource site and support forum](http://www.mympxplayer.org/)

[MTV Video topic-thread on moviecodec.com forum](http://www.moviecodec.com/topics/7595p1.html)

[MP4 Player Search - Community powered and driven](http://mp4-player-search-swicki.eurekster.com/)

[MP4 Nation Knowledge Base: Support and tips on MP4 Players](http://www.mp4nation.com/software/index.html)

[An introduction to MP4 players, what they do, what you can do to them and a review](http://www.imup2date.co.uk/MP4/)

[Recovering dead players and other software](http://www.s1mp3.org/en/docs_deadrec.php)

[MP4 Forums on S1MP3.org](http://forum.s1mp3.org/)

[MTV Video Forum](http://www.moviecodec.com/topics/7595p3.html)

[(A negative) video-review of one of these MP4-Players on Youtube](http://www.youtube.com/watch?v=us507LedkyY)

[Chinavasion - China direct wholesaler of MP4 Players using AMV video format](http://www.chinavasion.com/index.php/cName/mp4-players-2gb-mp4-players/)

[HOW TO: Converting movies to AMV with subtitle, without VirtualDub.](http://wiki.s1mp3.org/Convert_movie_to_amv_with_subtitle)














