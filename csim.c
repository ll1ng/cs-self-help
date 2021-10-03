#include "cachelab.h"
#include<stdio.h>
#include<string.h>
#include<getopt.h>
#include<stdlib.h>
typedef struct 
{
    int valid;
    unsigned long long tag;
    char *cache;    //完成lab只需对地址判断，不必实际分配存储空间
    int visit_time;
}line;
int s,E,b,S,B;
int hit=0,miss=0,eviction=0;
int timess=0;
int main(int argc,char *argv[])
{
    int opt;
    int vflag=0;
    
    FILE *fp;
    while((opt=getopt(argc,argv,"hvs:E:b:t:a:"))!=-1)
    {
        switch (opt)
        {
        case 'h':
            printf("Usage: ./csim-ref [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
            exit(0);
        
        case 'v'://是否显示文件内容
            vflag=1;    
            break;
        case 's':
            s=atoi(optarg);//标记位数
            S=1<<s;//组数
            break;

        case 'E'://一组的缓存行数
            E=atoi(optarg);
            break;
        
        case 'b'://一个高速缓存块的字节数
            b=atoi(optarg);
            B=1<<b;
            break;
        
        case 't':
            if((fp=fopen(optarg,"r"))==NULL)
            {
                printf("open file error! \n");
                exit(-1);
            }
            break;    
        
        default:            
            break;
        }
    }
    
    line** line_pointer=(line**)malloc(sizeof(line*)*S);
    int i,j;
    for(i=0;i<S;i++)
    {
        line_pointer[i]=(line*)malloc(sizeof(line)*s*E);
        for(j=0;j<E;j++)
        {
            line_pointer[i][j].valid=0;
        }        
    }
        
    char option;
    int size;
    unsigned long long addr;
    char ch;
        
    while(!feof(fp))
    {                   
        ch=fgetc(fp);
        fseek(fp,-1,SEEK_CUR);
        
        if(ch==32)
        {
            fscanf(fp," %c %llx,%x\n",&option,&addr,&size);
        }
        else if(ch=='I')
        {            
            fscanf(fp,"%c  %llx,%x\n",&option,&addr,&size);
            goto next;
        }
        else if(ch=='\n')
        {
            fscanf(fp,"\n%c  %llx,%x\n",&option,&addr,&size);
            goto next;
        }

        if(size!=1&&(((addr&0x3)!=0)||((size&0x3)!=0)))
        {
            goto next;
        }

        int index;        
        unsigned long long tag;

        index=(addr>>b)&(~((1LL<<63)>>(63-s)));
        tag=(addr>>(s+b));
		timess++;

        int min_times;
        int min_index;
        for(i=0;i<E;i++)
        {
            if(line_pointer[index][i].valid&&line_pointer[index][i].tag==tag)
            {
                if(option=='M')
                {
                    if(vflag)printf("hit hit\n");
                    hit=hit+2;
                }
                else 
                {
                    if(vflag)printf("hit\n");
                    hit++;
                }
                line_pointer[index][i].visit_time=timess;
                break;              
            }
            else if(!line_pointer[index][i].valid)
            {
                if(option=='M')
                {
                    if(vflag)printf("miss hit\n");
                    miss++;
                    hit++;
                }                
                else 
                {
                    if(vflag)printf("miss\n");
                    miss++;
                }             
                line_pointer[index][i].visit_time=timess;
                line_pointer[index][i].valid=1;
                line_pointer[index][i].tag=tag;
                break;
            }
        }
        if(i==E)
        {        
            min_index=0;
            min_times=line_pointer[index][0].visit_time;
            for(j=0;j<E;j++)
            {
                if(line_pointer[index][j].visit_time<min_times)
                {
                    min_index=j;
                    min_times=line_pointer[index][j].visit_time;
                }
            }
            line_pointer[index][min_index].visit_time=timess;
            line_pointer[index][min_index].tag=tag;
            if(option=='M')
            {
                if(vflag)printf("miss eviction hit\n");
                miss++;
                eviction++;
                hit++;
            }
            else
            {
                if(vflag)printf("miss eviction\n");
                miss++;
                eviction++;               
            }
        }
        next:if(!feof(fp))fseek(fp,-1,SEEK_CUR);
    }
    for(i=0;i<s;i++)
    {        
        free(line_pointer[i]);      
    }
    free(line_pointer);

    printSummary(hit,miss,eviction);
    return 0;
}
// void printSummary(int hits,int misses,int evictions)
// {
//     printf("hit:%d misses:%d evictions:%d\n",hits,misses,evictions);
// }