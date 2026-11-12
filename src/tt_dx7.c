#include "tt_synth.h"
#include <string.h>
int tt_dx7_max(int p) {
    static const int op[21]={99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14};
    static const int global[19]={99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48};
    return p<0 || p>=145?-1:p<126?op[p%21]:global[p-126];
}
int tt_dx7_valid(const TtDx7Patch *p) {
    if(!p)return 0;
    for(int i=0;i<145;++i)if(p->data[i]>tt_dx7_max(i))return 0;
    for(int i=145;i<155;++i)if(p->data[i]<32 || p->data[i]>126)return 0;
    return p->data[155]<=63;
}
void tt_dx7_init(TtDx7Patch *p) {
    memset(p,0,sizeof(*p));
    for(int op=0;op<6;++op) {
        uint8_t *d=p->data+op*21;
        d[0]=99;d[1]=70;d[2]=60;d[3]=65;
        d[4]=99;d[5]=99;d[6]=99;d[18]=1;d[20]=7;
    }
    p->data[5*21+16]=99;
    for(int i=0;i<4;++i){p->data[126+i]=99;p->data[130+i]=50;}
    p->data[136]=1;p->data[137]=35;p->data[141]=1;
    p->data[144]=24;p->data[155]=63;
    memcpy(p->data+145,"INIT VOICE",10);
}
int tt_dx7_import(const uint8_t *d,size_t size,int voice,TtDx7Patch *out) {
    int bank=size==4104;
    if(!d || !out || (size!=163 && !bank) || voice<0 || voice>=(bank?32:1) ||
       d[0]!=0xf0 || d[1]!=0x43 || d[2]>15 || d[3]!=(bank?9:0) ||
       d[4]!=(bank?32:1) || d[5]!=(bank?0:27) || d[size-1]!=0xf7)return 0;
    unsigned sum=0;
    for(size_t i=6;i<size-1;++i){if(d[i]>127)return 0;sum+=d[i];}
    if(sum&127)return 0;
    TtDx7Patch p={0};
    if(!bank)memcpy(p.data,d+6,155);
    else {
        const uint8_t *b=d+6+voice*128;
        for(int op=0;op<6;++op) {
            const uint8_t *s=b+op*17;uint8_t *t=p.data+op*21;
            memcpy(t,s,11);t[11]=s[11]&3;t[12]=(s[11]>>2)&3;
            t[13]=s[12]&7;t[20]=s[12]>>3;t[14]=s[13]&3;t[15]=s[13]>>2;
            t[16]=s[14];t[17]=s[15]&1;t[18]=s[15]>>1;t[19]=s[16];
        }
        memcpy(p.data+126,b+102,9);p.data[135]=b[111]&7;p.data[136]=b[111]>>3;
        memcpy(p.data+137,b+112,4);p.data[141]=b[116]&1;
        p.data[142]=(b[116]>>1)&7;p.data[143]=b[116]>>4;
        memcpy(p.data+144,b+117,11);
    }
    p.data[155]=63;
    if(!tt_dx7_valid(&p))return 0;
    *out=p;return 1;
}
