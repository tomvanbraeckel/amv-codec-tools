#ifndef G729A_H
#define G729A_H


void* g729a_encoder_init(void);
int g729a_decode_frame(void *context, short* ibuf, int ibuflen, short* obuf, int obuflen);
void g729a_encoder_uninit(void* context);

void* g729a_decoder_init(void);
int g729a_encode_frame(void *context, short* ibuf, int ibuflen, short* obuf, int obuflen);
void g729a_decoder_uninit(void* context);

#endif //G729A_H

