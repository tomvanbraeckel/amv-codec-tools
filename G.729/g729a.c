#include <stdlib.h>

#include "typedef.h"
#include "basic_op.h"
#include "ld8a.h"

 Word16 bad_lsf;        /* bad LSF indicator   */

void* g729a_encoder_init()
{
  Init_Pre_Process();
  Init_Coder_ld8a();
  return NULL;
}

int g729a_encode_frame(void * context, Word16* data, int ibuflen, Word16* serial, int obuflen)
{
    extern Word16 *new_speech;     /* Pointer to new speech data            */
    Word16 prm[PRM_SIZE];          /* Analysis parameters.                  */

    memcpy(new_speech, data, sizeof(Word16)*L_FRAME);

    Pre_Process(new_speech, L_FRAME);

    Coder_ld8a(prm);

    prm2bits_ld8k( prm, serial);
    return SERIAL_SIZE;
}

void g729a_encoder_uninit(void* context)
{
}

void* g729a_decoder_init()
{
  bad_lsf = 0;          /* Initialize bad LSF indicator */
  Init_Decod_ld8a();
  Init_Post_Filter();
  Init_Post_Process();
  return NULL;
}

int g729a_decode_frame(void* context, Word16* serial, int ibuflen, Word16* obuf, int obuflen){
  Word16  parm[PRM_SIZE+1];             /* Synthesis parameters        */
  Word16  Az_dec[MP1*2];                /* Decoded Az for post-filter  */
  Word16  T2[2];                        /* Pitch lag for 2 subframes   */
  Word16  synth_buf[L_FRAME+M];
  Word16* synth;
  int i;

    for (i=0; i<M; i++) synth_buf[i] = 0;
    synth = synth_buf + M;

    parm[0] = 0;           /* No frame erasure */
    for (i=2; i < SERIAL_SIZE; i++)
      if (serial[i] == 0 ) parm[0] = 1; /* frame erased     */

    bits2prm_ld8k( &serial[2], &parm[1]);

    parm[4] = Check_Parity_Pitch(parm[3], parm[4]);
    Decod_ld8a(parm, synth, Az_dec, T2);

//dmp_fp16("pf: speech_buf", synth-10, 40, 0);
//    Post_Filter(synth, Az_dec, T2);        /* Post-filter */
//    Post_Process(synth, L_FRAME);
    Copy(synth, obuf, L_FRAME);
    return L_FRAME;
}

void g729a_decoder_uninit(void* context)
{
}
