#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static long fib(long n){return n<2?n:fib(n-1)+fib(n-2);}
int main(int argc,char**argv){
  const char*k=argv[1]; double t0=now();
  if(!strcmp(k,"intloop")){long s=0;for(long i=0;i<20000000;i++)s=(s+i*i)%1000003;printf("%ld\n",s);}
  else if(!strcmp(k,"numloop")){double s=0,sg=1;for(long q=0;q<20000000;q++){s+=sg/(2.0*q+1.0);sg=-sg;}printf("%.15g\n",s*4);}
  else if(!strcmp(k,"dot")){int n=1000;double*a=malloc(8*n),*b=malloc(8*n);for(int i=0;i<n;i++){a[i]=i*0.5;b[i]=1.0/(i+1);}double t=0;for(int r=0;r<5000;r++){double s=0;for(int i=0;i<n;i++)s+=a[i]*b[i];t+=s;}printf("%.15g\n",t);}
  else if(!strcmp(k,"matmul")){int n=120;double*a=malloc(8*n*n),*b=malloc(8*n*n),*c=malloc(8*n*n);for(int i=0;i<n*n;i++){a[i]=(i%7)*0.25;b[i]=(i%5)*0.5;}for(int i=0;i<n;i++)for(int j=0;j<n;j++){double s=0;for(int q=0;q<n;q++)s+=a[i*n+q]*b[q*n+j];c[i*n+j]=s;}double t=0;for(int i=0;i<n*n;i++)t+=c[i];printf("%.15g\n",t);}
  else if(!strcmp(k,"fib")){printf("%ld\n",fib(27));}
  else if(!strcmp(k,"stencil")){int n=2000;double*u=calloc(n,8),*v;u[0]=u[n-1]=1;v=malloc(8*n);memcpy(v,u,8*n);for(int r=0;r<400;r++){for(int i=1;i<n-1;i++)v[i]=0.5*(u[i-1]+u[i+1]);for(int i=1;i<n-1;i++)u[i]=0.5*(v[i-1]+v[i+1]);}double t=0;for(int i=0;i<n;i++)t+=u[i];printf("%.15g\n",t);}
  fprintf(stderr,"%s %.4f s\n",k,now()-t0);
}
