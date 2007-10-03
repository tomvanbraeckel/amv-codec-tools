/*
 * copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FFMPEG_AMV_H
#define FFMPEG_AMV_H

#include "avcodec.h"

#define AMVF_HASINDEX           0x00000010        // Index at end of file?
#define AMVF_MUSTUSEINDEX       0x00000020
#define AMVF_ISINTERLEAVED      0x00000100
#define AMVF_TRUSTCKTYPE        0x00000800        // Use CKType to find key frames?
#define AMVF_WASCAPTUREFILE     0x00010000
#define AMVF_COPYRIGHTED        0x00020000

#define AMV_MAX_RIFF_SIZE       0x40000000LL
#define AMV_MASTER_INDEX_SIZE   256

/* index flags */
#define AMVIF_INDEX             0x10

#endif /* FFMPEG_AMV_H */
