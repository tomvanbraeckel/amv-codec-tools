# How to convert video into AMV format #

## Using patched FFmpeg ##

In common case command line will look like
```
ffmpeg -i <input> -f amv -s <width>x<height> -r 16 -ac 1 -ar 22050 -qmin 3 -qmax 3 <output>
```

FFmpeg accepts any picture resolutions, but hardware players supports only:
  * 128x90
  * 128x128
  * 160x120

Input file can be any video format supported by FFmpeg.

_Note: if output file has 'amv' extention, `-f amv` option can be omitted._

Option `-r 16` sets framerate to 16 frames/sec (other values seems to be not supported by hardware players).

Options `-ac 1` and `-ar 22050` sets 22050 Hz mono sound. **Other formats are not supported**.

Options `-qmin 3` and `-qmax 3` forces quantizer to be equal to 3. This will give you good quality with acceptable file size.

Example 1. Converting AVI file into AMV with 160x120 picture size:
```
ffmpeg -i file.avi -s 160x120 -ac 1 -ar 22050 -qmin 3 -qmax 3 file.amv
```