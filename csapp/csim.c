#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cachelab.h"

typedef struct
{
    int valid;
    unsigned long long tag;
    char *cache; 
    /* 
      to pass the lab's check, just need to analyse the visiting memory address,
      no need to malloc memory actually 
     */
    int visit_time;
} set;
int s, E, b, S, B;
int hit = 0, miss = 0, eviction = 0;
int timess = 0;// times cache is visited
int main(int argc, char *argv[])
{
    int opt;
    int vflag = 0;

    FILE *fp;
    while ((opt = getopt(argc, argv, "hvs:E:b:t:a:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            printf("Usage: ./csim-ref [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
            exit(0);

        case 'v': // whether to printf the content of tracefile
            vflag = 1;
            break;
        case 's':
            s = atoi(optarg); // number of bits to represent set index
            S = 1 << s;       // number of sets
            break;

        case 'E': // number of cache lines of a set
            E = atoi(optarg);
            break;

        case 'b': // number of bits to represent block offset
            b = atoi(optarg);
            B = 1 << b;
            break;

        case 't':
            if ((fp = fopen(optarg, "r")) == NULL)
            {
                printf("open file error! \n");
                exit(-1);
            }
            break;

        default:
            break;
        }
    }

    set **line_pointer = (set **)malloc(sizeof(set *) * S);
    int i, j;
    for (i = 0; i < S; i++)
    {
        line_pointer[i] = (set *)malloc(sizeof(set) * s * E);
        for (j = 0; j < E; j++)
        {
            line_pointer[i][j].valid = 0;
        }
    }

    char option;
    int size;
    unsigned long long addr;
    char ch;

    while (!feof(fp))
    {
        ch = fgetc(fp);
        fseek(fp, -1, SEEK_CUR);

        if (ch == 32)
        {
            fscanf(fp, " %c %llx,%x\n", &option, &addr, &size);
        }
        else if (ch == 'I')
        {
            fscanf(fp, "%c  %llx,%x\n", &option, &addr, &size);
            goto next;
        }
        else if (ch == '\n')
        {
            fscanf(fp, "\n%c  %llx,%x\n", &option, &addr, &size);
            goto next;
        }

        if (size != 1 && (((addr & 0x3) != 0) || ((size & 0x3) != 0)))
        {
            goto next;
        }

        int index;
        unsigned long long tag;

        index = (addr >> b) & (~((1LL << 63) >> (63 - s)));
        tag = (addr >> (s + b));
        timess++;

        int min_times;
        int min_index;
        for (i = 0; i < E; i++)
        {
            if (line_pointer[index][i].valid && line_pointer[index][i].tag == tag)
            {
                if (option == 'M')
                {
                    if (vflag)
                        printf("hit hit\n");
                    hit = hit + 2;
                }
                else
                {
                    if (vflag)
                        printf("hit\n");
                    hit++;
                }
                line_pointer[index][i].visit_time = timess;
                break;
            }
            else if (!line_pointer[index][i].valid)
            {
                if (option == 'M')
                {
                    if (vflag)
                        printf("miss hit\n");
                    miss++;
                    hit++;
                }
                else
                {
                    if (vflag)
                        printf("miss\n");
                    miss++;
                }
                line_pointer[index][i].visit_time = timess;
                line_pointer[index][i].valid = 1;
                line_pointer[index][i].tag = tag;
                break;
            }
        }
        if (i == E)
        {
            min_index = 0;
            min_times = line_pointer[index][0].visit_time;
            for (j = 0; j < E; j++)
            {
                if (line_pointer[index][j].visit_time < min_times)
                {
                    min_index = j;
                    min_times = line_pointer[index][j].visit_time;
                }
            }
            line_pointer[index][min_index].visit_time = timess;
            line_pointer[index][min_index].tag = tag;
            if (option == 'M')
            {
                if (vflag)
                    printf("miss eviction hit\n");
                miss++;
                eviction++;
                hit++;
            }
            else
            {
                if (vflag)
                    printf("miss eviction\n");
                miss++;
                eviction++;
            }
        }
    next:
        if (!feof(fp))
            fseek(fp, -1, SEEK_CUR);
    }
    for (i = 0; i < s; i++)
    {
        free(line_pointer[i]);
    }
    free(line_pointer);

    printSummary(hit, miss, eviction);
    return 0;
}
// void printSummary(int hits,int misses,int evictions)
// {
//     printf("hit:%d misses:%d evictions:%d\n",hits,misses,evictions);
// }