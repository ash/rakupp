#include <stdio.h>
#include <math.h>
typedef struct { double x,y,vx,vy,m; } B;
int main(){ B b[5]; for(int i=0;i<5;i++){b[i].x=i;b[i].y=i*0.5;b[i].vx=b[i].vy=0;b[i].m=1.0+i;}
 for(int r=0;r<20000;r++){ for(int i=0;i<5;i++) for(int j=i+1;j<5;j++){ B*bi=&b[i],*bj=&b[j];
   double dx=bi->x-bj->x, dy=bi->y-bj->y, d2=dx*dx+dy*dy+0.01, mag=0.001/(d2*sqrt(d2));
   bi->vx-=dx*bj->m*mag; bi->vy-=dy*bj->m*mag; bj->vx+=dx*bi->m*mag; bj->vy+=dy*bi->m*mag; }
   for(int i=0;i<5;i++){ b[i].x+=0.001*b[i].vx; b[i].y+=0.001*b[i].vy; } }
 double e=0; for(int i=0;i<5;i++) e+=0.5*b[i].m*(b[i].vx*b[i].vx+b[i].vy*b[i].vy); printf("%.15g\n",e); }
