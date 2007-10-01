#############################################################################
# QCELP Decoder / Source helper
# Copyright (c) 2007 Reynaldo H. Verdejo Pinochet
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with FFmpeg; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
#
#!/bin/bash

RSYNC=`which rsync`
PATCH=`which patch`

function missing()
{
    echo ""
    echo "You need patch and rsync somewhere in your PATH for this script"
    echo "to work properly"
    echo ""
}

function usage()
{
    echo ""
    echo "Simple source preparation script for ffmpeg's qcelp decoder."
    echo ""
    echo "Usage:"
    echo ""
    echo "$0 PATH_TO_FFMPEG_REPO"
    echo ""
    echo "PATH_TO_FFMPEG_REPO should point to the dir containing ffmpeg's trunk"
    echo ""
}

# Check usage

if [ $# -lt 1 ]; then
    usage
    exit 1
fi

# Check needed
for i in rsync patch; do
    if [ -z "`which $i`" ]; then
        echo "[ERROR] $i not found in $PATH!"
        missing
        exit 1
    fi
done

RSYNC=`which rsync`
PATCH=`which patch`

echo -n "syncing FFmpeg sources from $1: "

for i in `rsync -avz $1/ ffmpeg/` ; do true  ; done

if [ $? ]; then
    echo "Finished"
fi

# copying files and patching sources
echo ""
echo "Copying needed files and patching synced sources: "

cd ffmpeg
patch -p0 < ../amvmux_patch.diff
cp ../amvenc.c libavformat/
cp ../amv.h libavformat/
# we don't copy these because those differences are handled by the diff file
# cp ../mjpegenc.c libavcodec/
# cp ../adpcm.c libavcodec/
cd ..
echo "Finished"
echo ""
echo "Patched source is in trunk dir. happy hacking"

exit 0
echo "
