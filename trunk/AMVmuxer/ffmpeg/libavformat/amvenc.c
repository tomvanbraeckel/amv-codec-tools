/*
 * AMV muxer
 * Copyright (c) 2000 Fabrice Bellard.
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
#include "amv.h"
#include "riff.h"

/*
 * TODO:
 *  - fill all fields if non streamed (nb_frames for example)
 */

#ifdef CONFIG_AMV_MUXER


typedef struct {
    offset_t riff_start, movi_list, odml_list;
    offset_t frames_hdr_all, frames_hdr_strm[MAX_STREAMS];
    offset_t hours, minutes, seconds;
    int audio_strm_length[MAX_STREAMS];
    int riff_id;
    int packet_count[MAX_STREAMS];
    int last_stream_index;
} AMVContext;

static offset_t avi_start_new_riff(AMVContext *avi, ByteIOContext *pb,
                                   const char* riff_tag, const char* list_tag)
{
    offset_t loff;

    avi->riff_id++;

    avi->riff_start = start_tag(pb, "RIFF");
    put_tag(pb, riff_tag);
    loff = start_tag(pb, "LIST");
    put_tag(pb, list_tag);
    return loff;
}

static char* avi_stream2fourcc(char* tag, int index, enum CodecType type)
{
    tag[0] = '0';
    tag[1] = '0' + index;
    if (type == CODEC_TYPE_VIDEO) {
        tag[2] = 'd';
        tag[3] = 'c';
    } else {
        tag[2] = 'w';
        tag[3] = 'b';
    }
    tag[4] = '\0';
    return tag;
}

static int avi_write_counters(AVFormatContext* s, int riff_id)
{
    ByteIOContext *pb = &s->pb;
    AMVContext *avi = s->priv_data;
    int n, au_byterate, au_ssize, au_scale, nb_frames = 0;
    offset_t file_size;
    AVCodecContext* stream;

    file_size = url_ftell(pb);
    for(n = 0; n < s->nb_streams; n++) {
        assert(avi->frames_hdr_strm[n]);
        stream = s->streams[n]->codec;
        url_fseek(pb, avi->frames_hdr_strm[n], SEEK_SET);
        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);
        if(au_ssize == 0) {
            put_le32(pb, avi->packet_count[n]);
        } else {
            put_le32(pb, avi->audio_strm_length[n] / au_ssize);
        }
        if(stream->codec_type == CODEC_TYPE_VIDEO)
            nb_frames = FFMAX(nb_frames, avi->packet_count[n]);
    }
    if(riff_id == 1) {
        assert(avi->frames_hdr_all);
        url_fseek(pb, avi->frames_hdr_all, SEEK_SET);
        put_le32(pb, nb_frames);
        
        // HACK !
        int duration_seconds = nb_frames/s->streams[0]->codec->time_base.den;
        assert(avi->seconds);
        url_fseek(pb, avi->seconds, SEEK_SET);
        put_byte(pb,duration_seconds%60);
        assert(avi->minutes);
        url_fseek(pb, avi->minutes, SEEK_SET);
        put_byte(pb,duration_seconds/60);
        assert(avi->hours);
        url_fseek(pb, avi->hours, SEEK_SET);
        put_le16(pb,duration_seconds/3600);
    }
    url_fseek(pb, file_size, SEEK_SET);

    return 0;
}

static int avi_write_header(AVFormatContext *s)
{
    AMVContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int bitrate, n, i, nb_frames, au_byterate, au_ssize, au_scale;
    AVCodecContext *stream, *video_enc;
    offset_t list1, list2, strh, strf;

    avi->last_stream_index=1;

    /* header list */
    avi->riff_id = 0;
    list1 = avi_start_new_riff(avi, pb, "AMV ", "hdrl");

    /* avi header */
    put_tag(pb, "amvh");
    put_le32(pb, 14 * 4);
    bitrate = 0;

    video_enc = NULL;
    for(n=0;n<s->nb_streams;n++) {
        stream = s->streams[n]->codec;
        bitrate += stream->bit_rate;
        if (stream->codec_type == CODEC_TYPE_VIDEO)
            video_enc = stream;
    }

    nb_frames = 0;

    if(video_enc){
        put_le32(pb, (uint32_t)(INT64_C(1000000) * video_enc->time_base.num / video_enc->time_base.den));
    } else {
        put_le32(pb, 0);
    }
    put_le32(pb, bitrate / 8); /* XXX: not quite exact */
    put_le32(pb, 0); /* padding */
    if (url_is_streamed(pb))
        put_le32(pb, AMVF_TRUSTCKTYPE | AMVF_ISINTERLEAVED); /* flags */
    else
        put_le32(pb, AMVF_TRUSTCKTYPE | AMVF_HASINDEX | AMVF_ISINTERLEAVED); /* flags */
    avi->frames_hdr_all = url_ftell(pb); /* remember this offset to fill later */
    put_le32(pb, nb_frames); /* nb frames, filled later */
    put_le32(pb, 0); /* initial frame */
    put_le32(pb, s->nb_streams); /* nb streams */
    put_le32(pb, 1024 * 1024); /* suggested buffer size */
    if(video_enc){
        put_le32(pb, video_enc->width);
        put_le32(pb, video_enc->height);
    } else {
        put_le32(pb, 0);
        put_le32(pb, 0);
    }

    put_le32(pb, video_enc->time_base.den); // edited from AVI: framerate instead of reserved
    put_le32(pb, 1); // This is always 1 in a real AMV
    put_le32(pb, 0); /* reserved */
    
    avi->seconds = url_ftell(pb); /* remember this offset to fill later */
    put_byte(pb,0);
    avi->minutes = url_ftell(pb); /* remember this offset to fill later */
    put_byte(pb,0);
    avi->hours = url_ftell(pb); /* remember this offset to fill later */
    put_le16(pb, 0);     // duration minutes (filled in later)

    /* stream list */
    for(i=0;i<n;i++) {
        list2 = start_tag(pb, "LIST");
        put_tag(pb, "strl");

        stream = s->streams[i]->codec;

        /* stream generic header */
        strh = start_tag(pb, "strh");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO: put_tag(pb, "vids"); break;
        case CODEC_TYPE_AUDIO: put_tag(pb, "auds"); break;
        }
        if(stream->codec_type == CODEC_TYPE_VIDEO)
            put_le32(pb, stream->codec_tag);
        else
            put_le32(pb, 1);
        put_le32(pb, 0); /* flags */
        put_le16(pb, 0); /* priority */
        put_le16(pb, 0); /* language */
        put_le32(pb, 0); /* initial frame */

        if(stream->codec_type==CODEC_TYPE_AUDIO && i==1)
        {
            au_ssize=2;
            au_scale=s->streams[0]->time_base.num;
            au_byterate=s->streams[0]->time_base.den;
        }else
        ff_parse_specific_params(stream, &au_byterate, &au_ssize, &au_scale);

        put_le32(pb, au_scale); /* scale */
        put_le32(pb, au_byterate); /* rate */
        av_set_pts_info(s->streams[i], 64, au_scale, au_byterate);

        put_le32(pb, 0); /* start */
        avi->frames_hdr_strm[i] = url_ftell(pb); /* remember this offset to fill later */
        if (url_is_streamed(pb))
            put_le32(pb, AMV_MAX_RIFF_SIZE); /* FIXME: this may be broken, but who cares */
        else
            put_le32(pb, 0); /* length, XXX: filled later */

        /* suggested buffer size */ //FIXME set at the end to largest chunk
        if(stream->codec_type == CODEC_TYPE_VIDEO) {
            put_le32(pb, 1024 * 1024);
            put_le32(pb, -1); /* quality */
        } else if(stream->codec_type == CODEC_TYPE_AUDIO) {
            //put_le32(pb, 12 * 1024);
        } else 
            put_le32(pb, 0);
        put_le32(pb, au_ssize); /* sample size */
        put_le32(pb, 0);
        put_le16(pb, stream->width);
        put_le16(pb, stream->height);
        end_tag(pb, strh);

      if(stream->codec_type != CODEC_TYPE_DATA){
        strf = start_tag(pb, "strf");
        switch(stream->codec_type) {
        case CODEC_TYPE_VIDEO:
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            put_le32(pb, 0);
            break;
        case CODEC_TYPE_AUDIO:
            if (put_wav_header(pb, stream) < 0) {
                av_free(avi);
                return -1;
            }
	    put_le32(pb, 0);
            break;
        default:
            return -1;
        }
        end_tag(pb, strf);
      }

        end_tag(pb, list2);
    }

    end_tag(pb, list1);

    avi->movi_list = start_tag(pb, "LIST");
    put_tag(pb, "movi");

    put_flush_packet(pb);

    /*
     HACK!!!
     Set correct frame_size for audio stream
    */
    if(s->nb_streams > 1 && s->streams[1]->codec->codec_type == CODEC_TYPE_AUDIO) {
	    s->streams[1]->codec->frame_size=av_rescale(
		    s->streams[1]->codec->sample_rate,
		    s->streams[0]->codec->time_base.num,
		    s->streams[0]->codec->time_base.den);
    }

    return 0;
}


static int avi_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AMVContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    unsigned char tag[5];
    unsigned int flags=0;
    const int stream_index= pkt->stream_index;
    AVCodecContext *enc= s->streams[stream_index]->codec;
    int size= pkt->size;

//    av_log(s, AV_LOG_DEBUG, "%"PRId64" %d %d\n", pkt->dts, avi->packet_count[stream_index], stream_index);
    while(enc->block_align==0 && pkt->dts != AV_NOPTS_VALUE && pkt->dts > avi->packet_count[stream_index]){
        AVPacket empty_packet;

        av_init_packet(&empty_packet);
        empty_packet.size= 0;
        empty_packet.data= NULL;
        empty_packet.stream_index= stream_index;
        avi_write_packet(s, &empty_packet);
//        av_log(s, AV_LOG_DEBUG, "dup %"PRId64" %d\n", pkt->dts, avi->packet_count[stream_index]);
    }
    avi->packet_count[stream_index]++;

    avi_stream2fourcc(&tag[0], stream_index, enc->codec_type);
    if(pkt->flags&PKT_FLAG_KEY)
        flags = 0x10;
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
       avi->audio_strm_length[stream_index] += size;
    }

    put_buffer(pb, tag, 4);
    put_le32(pb, size);
    put_buffer(pb, pkt->data, size);
    //Data in AMV files are not aligned by 2 bytes
//    if (size & 1) put_byte(pb, 0);

    put_flush_packet(pb);
    return 0;
}

static int avi_write_trailer(AVFormatContext *s)
{
    AMVContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int res = 0;

    if (!url_is_streamed(pb)){
        if (avi->riff_id == 1) {
            end_tag(pb, avi->movi_list);
            put_tag(pb, "AMV_END_");	// Added by Tom from AMV compatibility
            end_tag(pb, avi->riff_start);
        }
    }
    avi_write_counters(s, avi->riff_id);
    put_flush_packet(pb);

    return res;
}

static void amv_queue_packet(AVPacket *pkt, AVPacketList** ppktl){
    AVPacketList* pktl, **next_point;

    pktl = av_mallocz(sizeof(AVPacketList));
    pktl->pkt= *pkt;

//        assert(pkt->destruct != av_destruct_packet); //FIXME

    if(pkt->destruct == av_destruct_packet)
        pkt->destruct= NULL; // non shared -> must keep original from being freed
    else
        av_dup_packet(&pktl->pkt);  //shared -> must dup

    next_point = ppktl;
    while(*next_point){
        next_point= &(*next_point)->next;
    }
    pktl->next= *next_point;
    *next_point= pktl;
}

static AVPacket amv_dequeue_packet(AVPacketList** ppktl){
    AVPacketList* pktl;
    AVPacket pkt;

    pktl=*ppktl;
    *ppktl=pktl->next;
    pkt=pktl->pkt;
    av_freep(&pktl);
    return pkt;
}

static int amv_interleave_packet(struct AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    AVPacketList *pktl;
    AMVContext* amv=s->priv_data;

    if(pkt)
        amv_queue_packet(pkt,&s->packet_buffer);

    if(s->packet_buffer && s->packet_buffer->pkt.stream_index!=amv->last_stream_index){
        *out=amv_dequeue_packet(&s->packet_buffer);
	amv->last_stream_index=out->stream_index;
        return 1;
    }
    pktl= s->packet_buffer;
    while(pktl && pktl->next){
        if(pktl->next->pkt.stream_index!=amv->last_stream_index)
            break;
        pktl=pktl->next;
    }

    if(pktl && pktl->next){
        *out=amv_dequeue_packet(&pktl->next);
	amv->last_stream_index=out->stream_index;
        return 1;
    }else{
        av_init_packet(out);
        return 0;
    }
}

AVOutputFormat amv_muxer = {
    "amv",
    "amv format",
    "video/amv",
    "amv",
    sizeof(AMVContext),
    CODEC_ID_ADPCM_IMA_AMV,
    CODEC_ID_AMV,
    avi_write_header,
    avi_write_packet,
    avi_write_trailer,
    .interleave_packet=amv_interleave_packet,
    .codec_tag= (const AVCodecTag*[]){codec_bmp_tags, codec_wav_tags, 0},
};
#endif //CONFIG_AMV_MUXER
