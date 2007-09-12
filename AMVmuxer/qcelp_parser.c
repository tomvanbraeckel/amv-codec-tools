/*
 * QCELP parser
 * Copyright (c) 2007 Reynaldo H. Verdejo Pinochet
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

/**
 * @file qcelp_parser.c
 * QCELP parser
 * @author Reynaldo H. Verdejo Pinochet
 */

#include "parser.h"
#include "qcelpdata.h"


/**
 * Finds the end of the current frame in the bitstream.
 *
 * @return the position of the first byte of the next frame, or -1
 */

static int qcelp_find_frame_end(ParseContext *pc, const uint8_t *buf,
       int buf_size)
{
    // Let's try and see if this packet holds exactly one frame

    switch(buf_size)
    {
        case 35:             // RATE_FULL in 'codec frame' fmt
        case 34:             // RATE_FULL
        case 17:             // RATE_HALF in 'codec frame' fmt
        case 16:             // RATE_HALF
        case  8:             // RATE_QUARTER in 'codec frame' fmt
        case  7:             // RATE_QUARTER
        case  4:             // RATE_OCTAVE in 'codec frame' fmt
        case  3:             // RATE_OCTAVE
            return buf_size;
    }

    /*
     * If we reach this point it means the packet holds a multiset of
     * frames, each one of them in codec frame format, all with the same
     * framerate, as described in:
     *
     * http://tools.ietf.org/html/draft-mckay-qcelp-02
     */

    if(buf_size < 3)
        return END_NOT_FOUND;

    switch(buf[0])
    {
        case 4:
            return 35;
        case 3:
            return 17;
        case 2:
            return  8;
        case 1:
            return  4;
    }

    return END_NOT_FOUND;
}

static int qcelp_parse(AVCodecParserContext *s, AVCodecContext *avctx,
       const uint8_t **poutbuf, int *poutbuf_size, const uint8_t *buf,
       int buf_size)
{
    int next;

    ParseContext *pc = s->priv_data;
    int start_found  = pc->frame_start_found;
    uint32_t state   = pc->state;

    start_found=1;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES)
        next=buf_size;
    else
    {
        next=qcelp_find_frame_end(pc, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0)
        {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;
}

AVCodecParser qcelp_parser = {
    { CODEC_ID_QCELP },
    sizeof(ParseContext),
    NULL,
    qcelp_parse,
    ff_parse_close,
};
