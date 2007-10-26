#ifndef G729A_H
#define G729A_H


int g729_encoder_init();
int g729_decode_frame(short* ibuf, int ibuflen, short* obuf, int obuflen);

int g729_decoder_init();
int g729_encode_frame(short* ibuf, int ibuflen, short* obuf, int obuflen);

#endif //G729A_H

