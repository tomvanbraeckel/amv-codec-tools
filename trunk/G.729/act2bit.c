#include <stdio.h>

#define iname "REC01.ACT"
#define oname "REC01.ACT.BIT"

int main(void){
  FILE* fi=fopen(iname, "rb");
  FILE* fo=fopen(oname, "wb");
  unsigned char buf[512];
  unsigned char buf2[10];

  const unsigned short czero=0x7f;
  const unsigned short cone=0x81;
  const unsigned short ctag=0x6b21;
  const unsigned short ctag2=80;
  unsigned short tmp;
  unsigned char c;
  int i,j,k;

  fseek(fi,512,SEEK_SET);

  while(!feof(fi))
  {
    fread(buf,512,1,fi);
    for(i=0;i<51;i++)
    {
        fwrite(&ctag,2,1,fo);
        fwrite(&ctag2,2,1,fo);
      buf2[1]=buf[i*10+0];
      buf2[3]=buf[i*10+1];
      buf2[5]=buf[i*10+2];
      buf2[7]=buf[i*10+3];
      buf2[9]=buf[i*10+4];
      buf2[0]=buf[i*10+5];
      buf2[2]=buf[i*10+6];
      buf2[4]=buf[i*10+7];
      buf2[6]=buf[i*10+8];
      buf2[8]=buf[i*10+9];
      for(j=0;j<10;j++)
      {
        c=buf2[j];
        for(k=0;k<8;k++)
        { 
         if(c&0x80) fwrite(&cone,2,1,fo); else fwrite(&czero,2,1,fo);
	 c<<=1;
        }
      }
    }
    if (buf[510] || buf[511]) printf("Warning! skipped: %02x %02x\n",buf[510],buf[511]);
  }
  fclose(fi);
  fclose(fo);
}
