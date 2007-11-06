/*
 * G.729 Annex A codec wrapper (around reference ITU's source)
 * Copyright (c) 2007 Vladimir Voroshilov
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

#include "avcodec.h"

#include "bitstream.h"

#include <g729a.h>

typedef struct
{
    unsigned char format;
#ifdef DEBUG_DUMP
    FILE* f;
    FILE* f2;
#endif
    void* priv;
} G729Context;

#define MAX_FORMATS 2

#define MP1 11


#ifdef G729_UNPACKED_LSP
#define VECTOR_SIZE 15
#define DEF_VECTOR(L0,L1,L2,L3,P1,P0,C1,S1,GA1,GB1,P2,C2,S2,GA2,GB2) {L0,L1,L2,L3,P1,P0,C1,S1,GA1,GB1,P2,C2,S2,GA2,GB2}
#else
#define VECTOR_SIZE 11
#define DEF_VECTOR(L0,L1,L2,L3,P1,P0,C1,S1,GA1,GB1,P2,C2,S2,GA2,GB2) {(L0+L1),(L2+L3),P1,P0,C1,S1,(GA1+GB1),P2,C2,S2,(GA2+GB2)}
#endif


#define DEF_FRAME(frames, L0,L1,L2,L3,P1,P0,C1,S1,GA1,GB1,P2,C2,S2,GA2,GB2) (frames),(frames*(L1+L0+L1+L2+L3+P1+P0+C1+S1+GA1+GB1+P2+C2+S2+GA2+GB2)/8),DEF_VECTOR(L0,L1,L2,L3,P1,P0,C1,S1,GA1,GB1,P2,C2,S2,GA2,GB2) 

static const struct{
    char* name;
    int sample_rate;
    int frames;
    short frame_size;
    char vector_bits[VECTOR_SIZE];
    char silence_compression;
} formats[MAX_FORMATS]={
  {"8Kb/s",   8000, DEF_FRAME(1, 1,7,5,5,8,1,13, 4,3,4,5,13, 4,3,4), 0},
#ifdef G729_SUPPORT_4400
  {"4.4Kb/s", 4400, DEF_FRAME(2, 1,7,5,5,8,1,12, 9,3,4,5,12, 9,3,4), 0},
#endif //G729_SUPPORT_4400
  { NULL,     0,    DEF_FRAME(0,0,0,0,0,0,0, 0, 0,0,0,0, 0, 0,0,0), 0}
};

#ifdef CONFIG_ENCODERS
static int ff_g729a_encoder_init(AVCodecContext * avctx)
{
    G729Context *ctx=avctx->priv_data;

    ctx->priv=g729a_encoder_init();

    if(avctx->channels!=1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only one channel is suported\n");
        return -1;
    }
    for(ctx->format=0; formats[ctx->format].name; ctx->format++)
        if(formats[ctx->format].sample_rate==avctx->sample_rate)
            break;
    if(!formats[ctx->format].name){
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d is not supported\n", avctx->sample_rate);
        return -1;
    }

    avctx->frame_size=formats[ctx->format].frame_size*8;
    avctx->block_align=avctx->frame_size;

    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);
    avctx->coded_frame->key_frame = 1;

    return 0;
}

static int ff_g729a_encode_frame(AVCodecContext *avctx,
                            uint8_t *dst, int buf_size, void *data)
{
    G729Context *ctx=avctx->priv_data;
    uint16_t serial[200];
    PutBitContext pb;
    int idx=2,i,k;

    g729a_encode_frame(ctx->priv, data, 0/* not used yet*/, serial, buf_size);

    init_put_bits(&pb, dst, buf_size*8);
    for(i=0;i<formats[ctx->format].frame_size;i++)
        for(k=0; k<8; k++){
            put_bits(&pb, 1, serial[idx++]==0x81?1:0);
        }

    return (put_bits_count(&pb)+7)/8;
}
#endif //CONFIG_ENCODERS

static int ff_g729a_decoder_init(AVCodecContext * avctx)
{
    G729Context *ctx=avctx->priv_data;

    for(ctx->format=0; formats[ctx->format].name; ctx->format++)
        if(formats[ctx->format].sample_rate==avctx->sample_rate)
            break;
    if(!formats[ctx->format].name){
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d is not supported\n", avctx->sample_rate);
        return -1;
    }

    ctx->priv=g729a_decoder_init();
#ifdef DEBUG_DUMP
ctx->f=fopen("test2.bit","wb");
ctx->f2=fopen("test2.raw","wb");
#endif

    avctx->frame_size=10;
    return 0;
}

static int ff_g729a_close(AVCodecContext *avctx)
{
    G729Context *ctx=avctx->priv_data;
#ifdef DEBUG_DUMP
    if(ctx->f) fclose(ctx->f);
    if(ctx->f2) fclose(ctx->f2);
#endif
    g729a_decoder_uninit(ctx->priv);
    ctx->priv=NULL;
    return 0;
}

static int ff_g729a_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    G729Context *ctx=avctx->priv_data;
    GetBitContext gb;
    int i,j,k;
    int l_frame=formats[ctx->format].frame_size*8;
    uint16_t serial[200];

    init_get_bits(&gb, buf, buf_size);

    *data_size=0;
    for(j=0; j<formats[ctx->format].frames; j++){
        int dst=0;
        serial[dst++]=0x6b21;
        serial[dst++]=80;
        
        for(i=0;i<FFMIN((200-2)/8, buf_size);i++)
            for(k=0; k<8; k++){
                serial[dst++]=get_bits1(&gb)?0x81:0x7f;
            }
#ifdef DEBUG_DUMP
        fwrite(serial,sizeof(uint16_t),l_frame+2,ctx->f);
#endif
        g729a_decode_frame(ctx->priv,serial, 0/*not used yet*/,(short*)data+j*l_frame, l_frame);
        *data_size+=2*l_frame;
    }
#ifdef DEBUG_DUMP
    fwrite(data,1,*data_size,ctx->f2);
#endif
    return buf_size;
}

#ifdef CONFIG_ENCODERS
AVCodec g729a_encoder = {
    "g729a",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729A,
    sizeof(G729Context),
    ff_g729a_encoder_init,
    ff_g729a_encode_frame,
    ff_g729a_close,
};
#endif //CONFIG_ENCODERS

AVCodec g729a_decoder = {
    "g729a",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729A,
    sizeof(G729Context),
    ff_g729a_decoder_init,
    NULL,
    ff_g729a_close,
    ff_g729a_decode_frame,
};
