#include <stdio.h>

#define GOOD_FILE "hole_correct.amv"
#define BAD_FILE "hole.amv"

#define VIDEO_SECT_ID 0x63643030 //00dc
#define AUDIO_SECT_ID 0x62773130 //01wb

int main(void)
{
    FILE *fgood,*fbad;
    char buf[200];
    unsigned int good_sect_len,bad_sect_len;
    unsigned int good_sect_id,bad_sect_id;
    unsigned int video_num,audio_num;
    unsigned int samples=0;
    unsigned int good_samples,bad_samples;
    fgood=fopen(GOOD_FILE,"rb");
    if(!fgood) {
        printf("Error opening good file '%s'\n",GOOD_FILE);
        return -1;
    }
    fbad=fopen(BAD_FILE,"rb");
    if(!fbad) {
        printf("Error opening bad file '%s'\n",BAD_FILE);
        fclose(fgood);
	return -1;
    }
    while(1) {
        fseek(fgood,0x138,SEEK_SET);
        fread(buf,4,1,fgood);
        if(strncmp(buf,"movi",4)){
            printf("Wring header size in '%s'\n",GOOD_FILE);
            break;
	}
        fseek(fbad,0x138,SEEK_SET);
        fread(buf,4,1,fbad);
        if(strncmp(buf,"movi",4)){
            printf("Wring header size in '%s'\n",BAD_FILE);
            break;
	}
        video_num=0;
        audio_num=0;
        while(!feof(fgood) && !feof(fbad)) {
            printf("Offset: good=0x%x, bad=0x%x\n",ftell(fgood),ftell(fbad));
            fread(&good_sect_id,4,1,fgood);
            fread(&bad_sect_id,4,1,fbad);
            if(!strncmp(&good_sect_id,"AMV_",4)){
                printf("EOF reached in '%s'\n",GOOD_FILE);
                break;
            }
            if(!strncmp(&bad_sect_id,"AMV_",4)){
                printf("EOF reached in '%s'\n",BAD_FILE);
                break;
            }
	    if(good_sect_id!=VIDEO_SECT_ID && good_sect_id!=AUDIO_SECT_ID){
                printf("Wrong sect id in '%s' at offset 0x%X: :0x%X\n",GOOD_FILE,ftell(fgood),good_sect_id);
                break;
            }
	    if(bad_sect_id!=VIDEO_SECT_ID && bad_sect_id!=AUDIO_SECT_ID){
                printf("Wrong sect id in '%s':0x%X\n",BAD_FILE,bad_sect_id);
                break;
            }
            if(good_sect_id!=bad_sect_id) {
                printf("Sect id are not equal: bad=0x%X, good=0x%X\n",good_sect_id,bad_sect_id);
                break;
            }
            if(good_sect_id==VIDEO_SECT_ID) video_num++;
            if(good_sect_id==AUDIO_SECT_ID) audio_num++;
            fread(&good_sect_len,4,1,fgood);
            fread(&bad_sect_len,4,1,fbad);
            if(good_sect_id==AUDIO_SECT_ID) samples+=(bad_sect_len-8)*2+1;
            if(good_sect_len!=bad_sect_len) {
                printf("Sect length for %.4s (bad offset 0x%x) #%d are not equal: good=0x%X, bad=0x%X %d\n",
                        &good_sect_id,ftell(fbad),(video_num+audio_num),good_sect_len,bad_sect_len,samples);
            }else{
                printf("Sect length for %.4s (bad offset 0x%x) #%d is equal: 0x%X (%d)\n",
                        &good_sect_id,ftell(fbad),(video_num+audio_num),good_sect_len,samples);
            }
            if(good_sect_id==AUDIO_SECT_ID){
                fseek(fbad,4,SEEK_CUR);
                fseek(fgood,4,SEEK_CUR);
                fread(&bad_samples,4,1,fbad);
                fread(&good_samples,4,1,fgood);
                printf("Samples count for %.4s (bad offset 0x%x) #%d are %s equal: good=0x%X, bad=0x%X %d\n",
                        &good_sect_id,ftell(fbad),(video_num+audio_num),good_samples==bad_samples?"":"not",good_samples,bad_samples);
                fseek(fgood,good_sect_len-8,SEEK_CUR);
                fseek(fbad,bad_sect_len-8,SEEK_CUR);
            }else{
            fseek(fgood,good_sect_len,SEEK_CUR);
            fseek(fbad,bad_sect_len,SEEK_CUR);
            }

        }
        printf("Check successfully finished\n");
        break;
    }
    
    fclose(fgood);
    fclose(fbad);
    return 0;
}
