/*
 * ACT file format muxer/demuxer
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
#include "avformat.h"
#include "riff.h"

#define CHUNK_SIZE 512
#define RIFF_TAG MKTAG('R','I','F','F')
#define WAVE_TAG MKTAG('W','A','V','E')

typedef  struct{
    uint8_t tag;    //??? 0x84
    uint16_t msec;  ///< duration msec
    uint8_t  sec;   ///< duration sec
    uint32_t minutes;   ///< Duration min
} ACTHeader;

typedef struct{
    int bytes_left_in_chunk;
    ACTHeader hdr;
    offset_t data;
    offset_t riff;
    int frames;
} ACTContext;

#ifdef CONFIG_MUXERS
static int act_write_header(AVFormatContext *s)
{
    ACTContext* ctx = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    offset_t tmp;
    int i;

    if (enc->codec_id != CODEC_ID_G729A)
        return -1;

    ctx->riff=start_tag(pb, "RIFF");       /* magic number */
    put_buffer(pb, "WAVE",4);
    tmp=start_tag(pb, "fmt ");
    put_le16(pb, 0x01);
    put_le16(pb, 0x01);
    put_le32(pb, enc->sample_rate);
    put_le32(pb, enc->sample_rate*2);
    put_le16(pb, 2);
    put_le16(pb, 16);
    end_tag(pb, tmp);    
    ctx->data=start_tag(pb, "data");
    tmp=url_ftell(pb);
    for(i=0;i<512-tmp;i++)
        put_byte(pb, 0);

    ctx->frames=0;
    put_flush_packet(pb);
    return 0;
}

static int act_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ACTContext* ctx = s->priv_data;
    uint8_t *data=pkt->data;
    int i;

    if(!ctx->bytes_left_in_chunk)
        ctx->bytes_left_in_chunk=CHUNK_SIZE;

    put_byte(&s->pb, data[1]);
    put_byte(&s->pb, data[3]);
    put_byte(&s->pb, data[5]);
    put_byte(&s->pb, data[7]);
    put_byte(&s->pb, data[9]);
    put_byte(&s->pb, data[0]);
    put_byte(&s->pb, data[2]);
    put_byte(&s->pb, data[4]);
    put_byte(&s->pb, data[6]);
    put_byte(&s->pb, data[8]);

    ctx->bytes_left_in_chunk-=pkt->size;
    
    if(ctx->bytes_left_in_chunk<pkt->size)
        for(; ctx->bytes_left_in_chunk; ctx->bytes_left_in_chunk--)
            put_byte(&s->pb, 0);

    put_flush_packet(&s->pb);
    ctx->frames++;
    return 0;
}
static int act_write_trailer(AVFormatContext *s)
{
    ACTContext* ctx = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    int duration;
    offset_t tmp;

    tmp=url_ftell(pb);
    tmp%=512;
    for(; tmp<512; tmp++)
        put_byte(pb, 0);

    end_tag(pb, ctx->data);
    end_tag(pb, ctx->riff);
    duration=av_rescale(ctx->frames, 1000*enc->frame_size, enc->sample_rate);
av_log(s, AV_LOG_ERROR, "Duration %d\n", duration);

    url_fseek(pb, 256, SEEK_SET);
    put_byte(pb, 0x84);
    put_le16(pb, duration % 1000); //milliseconds
    duration/=1000;
    put_byte(pb, duration %60); //seconds
    duration/=60;
    put_le32(pb, duration); //minutes
    
    put_flush_packet(pb);
    return 0;
}
#endif //CONFIG_MUXERS

static int act_probe(AVProbeData *p)
{
    ACTHeader* hdr=(ACTHeader*)&p->buf[256];

    if ((AV_RL32(&p->buf[0]) != RIFF_TAG) ||
        (AV_RL32(&p->buf[8]) != WAVE_TAG) ||
        (AV_RL32(&p->buf[16]) != 16))
    return 0;

    if(hdr->tag!=0x84)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int act_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    ACTContext* ctx = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int size;
    AVStream* st;

    st=av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    url_fskip(pb, 16);
    size=get_le32(pb);
    get_wav_header(pb, st->codec, size);

    url_fseek(pb, 256, SEEK_SET);
    if (get_buffer(pb, (char*)&ctx->hdr, sizeof(ctx->hdr))!=sizeof(ctx->hdr))
        return AVERROR(EIO);


    st->codec->codec_tag = 0;
    st->codec->codec_id=CODEC_ID_G729A;
    st->codec->frame_size=10;

    st->duration=(ctx->hdr.minutes*60+ctx->hdr.sec)*100+ctx->hdr.msec/10;

    av_set_pts_info(st, 64, 1, 800);

    if(st->codec->sample_rate!=8000 && st->codec->sample_rate!=4400)
    {
        av_log(s, AV_LOG_ERROR, "Sample rate %d is not supported\n", st->codec->sample_rate);
        return -1;
    }


    ctx->bytes_left_in_chunk=CHUNK_SIZE;

    url_fseek(pb, 512, SEEK_SET);
    return 0;
}


static int act_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    ACTContext* ctx = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint8_t bFrame[22];
    uint8_t *pkt_buf;
    int i, bytes_read;
    int frame_size=s->streams[0]->codec->frame_size;
    
    bytes_read = get_buffer(pb, bFrame, frame_size);

    if(bytes_read != frame_size || av_new_packet(pkt, frame_size))
        return AVERROR(EIO);

    pkt_buf=(uint16_t*)pkt->data;

    pkt_buf[1]=bFrame[0];
    pkt_buf[3]=bFrame[1];
    pkt_buf[5]=bFrame[2];
    pkt_buf[7]=bFrame[3];
    pkt_buf[9]=bFrame[4];
    pkt_buf[0]=bFrame[5];
    pkt_buf[2]=bFrame[6];
    pkt_buf[4]=bFrame[7];
    pkt_buf[6]=bFrame[8];
    pkt_buf[8]=bFrame[9];

    ctx->bytes_left_in_chunk -= frame_size;

    if(ctx->bytes_left_in_chunk < frame_size)
    {
        url_fskip(pb, ctx->bytes_left_in_chunk);
        ctx->bytes_left_in_chunk=CHUNK_SIZE;
    }

    return 0;
}
#ifdef CONFIG_MUXERS
AVOutputFormat act_muxer = {
    "act",
    "ACT",
    "audio/act",
    "act",
    sizeof(ACTContext),
    CODEC_ID_G729A,
    CODEC_ID_NONE,
    act_write_header,
    act_write_packet,
    act_write_trailer,
};
#endif //CONFIG_MUXERS

AVInputFormat act_demuxer = {
    "act",
    "ACT Voice file format",
    sizeof(ACTContext),
    act_probe,
    act_read_header,
    act_read_packet
};
