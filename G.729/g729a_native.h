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

#define av_log(ctx,lvl,fmt,param1) printf(fmt,param1)

typedef struct
{
    void* priv_data;
    int sample_rate;
    int frame_size;
} AVCodecContext;

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
