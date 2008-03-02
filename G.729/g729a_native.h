#ifndef H_G729_NATIVE_H
#define H_G729_NATIVE_H

#define av_free(ptr) if(ptr) free(ptr)
#define av_mallocz(A) calloc(A,1)
#define av_malloc(A) malloc(A)
#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define AVERROR(x) x

#define ENOMEM -1
#define EIO -2
#define AVERROR_NOFMT -3

#define av_log(ctx,lvl,fmt,param1) printf(fmt,param1)

typedef struct
{
    void* priv_data;
    int sample_rate;
    int frame_size;
    int channels;
} AVCodecContext;

#define     CODEC_TYPE_AUDIO 1
#define     CODEC_ID_G729A   1

typedef struct{
  char* name;
  int type;
  int id;
  int size;
    int (*init)(AVCodecContext *);
    int (*encode)(AVCodecContext *, uint8_t *buf, int buf_size, void *data);
    int (*close)(AVCodecContext *);
    int (*decode)(AVCodecContext *, void *outdata, int *outdata_size,
                  const uint8_t *buf, int buf_size);
}AVCodec;

typedef struct {
  int16_t *buf;
  int idx;
  int buf_size;
} GetBitContext;

static int init_get_bits(GetBitContext* pgb, const unsigned char* buf, int buf_size)
{
    pgb->buf=(uint16_t *)buf;
    pgb->buf+=2;//skip syncwork and size
    pgb->idx=0;
    pgb->buf_size=buf_size;
    return 0;
}

static int get_bits1(GetBitContext* pgb)
{
    return 0;
}

static int get_bits(GetBitContext* pgb, int n)
{
    int j, tmp=0;

    if(n>32 || pgb->idx+n >= pgb->buf_size) return 0;

    for(j=0; j<n; j++)
    {
        tmp<<= 1;
        tmp |= pgb->buf[pgb->idx++]==0x81?1:0;
    }
    return tmp;
}

static void dmp_d(char* name, float* arr, int size)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", arr[i]);
    }
    printf("\n");
}
static void dmp_fp16(char* name, short* arr, int size, int base)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", (1.0*arr[i])/(1<<base));
    }
    printf("\n");
}
static void dmp_fp32(char* name, int* arr, int size, int base)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", (1.0*arr[i])/(1<<base));
    }
    printf("\n");
}

#endif /*  H_G729_NATIVE_H */
