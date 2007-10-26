#ifndef G729A_H
#define G729A_H


int g729a_encoder_init();
int g729a_decode_frame(short* ibuf, int ibuflen, short* obuf, int obuflen);

int g729a_decoder_init();
int g729a_encode_frame(short* ibuf, int ibuflen, short* obuf, int obuflen);

#endif //G729A_H

