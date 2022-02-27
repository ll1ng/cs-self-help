/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * allocated chunk
 * offset: 0-3
 * 0|____size|alloc____|
 * 4|_____payload______|
 * 8|_____payload______|
 * c|____size|alloc____|
 * 
 * 
 * freed chunk(both small and large)
 * offset: 0-3
 * 0|____size|alloc____|
 * 4|_____prev_free____|
 * 8|_____back_free____|
 * c|____size|alloc____|
 * Free chunk list can be visited by free_pointer_list which points to the last freed 
 * chunk whose data structure is single linked list.
 * When chunk's size<=MAX_FASTBIN_SIZE, call it small chunk. Otherwise call it large chunk.
 * Sort free chunks by size 0x10, 0x18, ..., 0x58, {0x60~0x200}, {0x210-0x400},{0x410-0x1000}, {0x1008-0x3000}
 * ps when chunk is small chunk, alloc is always 1.
 * 
 * When to call consolidate?
 * When asked size>MAX_FASTBIN_SIZE and cannot find an appropriate chunk to store, consolidate all freechunks
 * in bins. Then insert them to the first bin. Tranverse all freechunks in the first bin, consolidate them.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* Basic constants and macros */
#define alloc      0x1
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */ 


#define MAX(x, y) ((x) > (y)? (x) : (y))  
#define MIN(x,y) ((x) < (y)? (x) : (y))
/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (unsigned int)(val))    

#define SET_PREV(p,prev) (*(unsigned int*)((char *)p+WSIZE)=(unsigned int)(prev))
#define SET_BACK(p,back) (*(unsigned int*)((char *)p+2*WSIZE)=(unsigned int)(back))
#define SET_ALLOC(p,alloc) (*(unsigned int*)(char *)p=(unsigned int)(GET_SIZE(p)|alloc))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV(p) (GET((char *)p+WSIZE))
#define GET_BACK(p) (GET((char *)p+WSIZE*2))

/* Given chunk ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) )
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - WSIZE)

#define SET_HF(p,size,alloc) ({\
    PUT(p,PACK(size,alloc));\
    PUT(p+size-WSIZE,PACK(size,alloc));\
    })
#define CLEAR_USE(p) ({\
    PUT(p,GET_SIZE(p));\
    PUT(p+GET_SIZE(p)-WSIZE,GET_SIZE(p));\
    })

/* Given chunk ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) ))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) -WSIZE)))
/* $end mallocmacros */

/*declaration of bins*/
#define MAX_FASTBIN_SIZE 0x58
#define MAX_FASTBIN_COUNT (MAX_FASTBIN_SIZE>>3)
#define UNSORTED_COUNT 4

#define has_fastbins(fastbins) \
({\
    int idx=0;int flag=0;\
    for(;idx<=MAX_FASTBIN_COUNT+UNSORTED_COUNT;idx++)\
    {\
        if((char**)fastbins[idx])flag=1;\
    }\
    flag;\
})

#define clear_inuse_fastbins(fastbins) \
({\
    int temp=MAX_FASTBIN_COUNT+UNSORTED_COUNT; \
    char *p; \
    do \
    { \
        p=fastbins[temp];\
        while(p)\
        {\
            CLEAR_USE(p);\
            p=(char*)GET_PREV(p);\
        }\
        temp--;\
    } while (temp>=0); })

#define cleanbins(bins) \
({\
    int idx;\
    for(idx=0;idx<=MAX_FASTBIN_COUNT+UNSORTED_COUNT;idx++)\
    *(unsigned int*)((char **)bins+idx)=0;\
})

/*
 * consolidate prev and next chunk of bp 
 * modify size and PREV
 */
#define unlink(p,BK,FD) \
({ \
    BK=(char*)GET_BACK(p);\
    FD=(char*)GET_PREV(p);\
    if(unlink_debug)\
    {\
        printf("unlink p: %p FD: %p BK: %p size: 0x%x\n",(char*)p,FD,BK,GET_SIZE(p));\
        printf("BK: %p",BK);fflush(stdout);\
        printf("cmp: %d\n",*(char**)BK==(char*)p);\
    }\
    if(*(char**)BK==(char*)p)\
    {\
        if(FD)SET_BACK(FD,BK);\
        *(char**)BK=FD; \
    }\
    else\
    {\
        SET_PREV(BK,FD);\
        if(FD)SET_BACK(FD,BK);\
    }\
    SET_BACK(p,0);SET_PREV(p,0);\
    if(unlink_debug)\
    {\
        printf("unlink p: %p FD: %p BK: %p size: 0x%x successful\n",p,FD,BK,GET_SIZE(p));showbins();\
    }\
})

#define insert2bins(freechunk,size) \
({\
    char **bin,*prev;\
    if(size<=MAX_FASTBIN_SIZE)\
    {\
        bin=&free_pointer_list[size>>3];\
    }\
    else\
    {\
        if(size<=0x200)\
        {\
            bin=&free_pointer_list[MAX_FASTBIN_COUNT+1];\
        }\
        else if(size<=0x400)\
        {\
            bin=&free_pointer_list[MAX_FASTBIN_COUNT+2];\
        }\
        else if(size<=0x1000)\
        {\
            bin=&free_pointer_list[MAX_FASTBIN_COUNT+3];\
        }\
        else if(size<=0x3000)\
        {\
            bin=&free_pointer_list[MAX_FASTBIN_COUNT+4];\
        }\
        else\
        {\
            bin=&free_pointer_list[0];\
        }\
    }\
    prev=*bin;\
    SET_PREV(freechunk,prev);\
    SET_BACK(freechunk,bin);\
    if(prev)SET_BACK(prev,freechunk);\
    *bin=freechunk;\
})

#define insert2unsorted(p) \
({\
    SET_BACK(p,&free_pointer_list[0]);\
    SET_PREV(p,free_pointer_list[0]);\
    if(free_pointer_list[0])SET_BACK(free_pointer_list[0],p);\
    free_pointer_list[0]=p;\
})

/*declaration of heap_listp*/
static char* heap_listp;
static char* free_pointer_list[MAX_FASTBIN_COUNT+UNSORTED_COUNT+1]={NULL};
//
/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "",
    /* First member's full name */
    "",
    /* First member's email address */
    "",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*declaration of functions*/
static void *extend_heap(int words);
static void *find_fit(size_t size);
static void *search4list(int size);
void consolidate();
void showbins();
/* 
 * mm_init - initialize the malloc package.
 * After the function's called, the initial chunk's organized like this:
 * offset: 0-7
 * 0 |___0___|___9___|<-heap_listp
 * 8 |___9___|___1___|
 * 
 */
int mm_init(void)
{
    cleanbins(free_pointer_list);

    if((heap_listp=mem_sbrk(4*WSIZE))==(void*)-1)return -1;
    PUT(heap_listp,0);
    PUT(heap_listp+(1*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp+(2*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp+(3*WSIZE),PACK(0,1));
    heap_listp+=2*WSIZE;
    
    // if(extend_heap(CHUNKSIZE/WSIZE)==NULL)return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{    
    if(size==0)return NULL;
    if(size<=DSIZE)size=2*DSIZE;
    else size = ALIGN(size + SIZE_T_SIZE);

    // int extendsize=MIN(size,CHUNKSIZE);
    int extendsize=size;
    void *bp = find_fit(size);

    if(!bp&&((bp=extend_heap(extendsize/WSIZE))==NULL))return NULL;
    
    SET_HF(bp,size,1);
#if request_debug
    printf("\e[31;49;1mmalloc: %p\nsize: %d 0x%x\nalloc: %d\n----------------------\n\e[39;49;0m",bp,GET_SIZE(bp),GET_SIZE(bp),GET_ALLOC(bp));
    if(has_fastbins(free_pointer_list))showbins();
#endif
    return bp+2*WSIZE;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{    
    ptr=(char*)ptr-2*WSIZE;
    int size=GET_SIZE(ptr);
    memset(ptr+WSIZE,0,size-DSIZE);

    if(GET_SIZE(ptr)>MAX_FASTBIN_SIZE)
    {
        SET_HF(ptr,size,0);
    }
    // if(size>MAX_FASTBIN_SIZE&&GET_SIZE(ptr+size)==0&&GET_ALLOC(ptr+size))
    // {
    //     PUT(ptr+size,0);
    //     if((extend_heap(-size/WSIZE))==NULL)
    //     {
    //         printf("shrink error\n");
    //         exit(-1);
    //     } 
    // }
    // else
    // {
        insert2bins(ptr,GET_SIZE(ptr));
    // }

#if request_debug     
    printf("\e[32;49;1mfree: %p\nsize: %d 0x%x\nalloc: %d\n-----------------\n\e[39;49;0m",ptr,GET_SIZE(ptr),GET_SIZE(ptr),GET_ALLOC(ptr));
#endif
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, int size)
{
    void *oldptr = ptr;
    void *chunkptr=ptr-DSIZE;
    void *newptr;
    size_t copySize;
    size=ALIGN(size+SIZE_T_SIZE);

    int oldsize=GET_SIZE(chunkptr);
    
    if(oldsize==size)return ptr;
    
    /* last chunk in heap */
    if(GET_SIZE(chunkptr+oldsize)==0&&GET_ALLOC(chunkptr+oldsize))
    {
        if((extend_heap((size-oldsize)/WSIZE))==NULL)
        {
            printf("shrink error\n");
            exit(-1);
        }
        SET_HF(chunkptr,size,1);       
        newptr=oldptr;
    }
    else
    {
        if((newptr=(extend_heap(size/WSIZE)))==NULL)
        {
            printf("shrink error\n");
            exit(-1);
        }
        newptr+=DSIZE;
        copySize=oldsize>size?size:oldsize;
        memcpy(newptr-WSIZE,oldptr-WSIZE,copySize-DSIZE);
        mm_free(oldptr);
    }
    
#if request_debug     
    printf("\033[1;33mrealloc: %p\nsize: %d 0x%x\nalloc: %d\n-----------------\n\e[39;49;0m",newptr-DSIZE,GET_SIZE(newptr-DSIZE),GET_SIZE(newptr-DSIZE),GET_ALLOC(newptr-DSIZE));
#endif
    return newptr;
}

/*
 * extend_heap - extend heap size by mem_sbrk
 */
static void *extend_heap(int words)
{
    char* bp;
    int size;
    
    size=(words%2)?(words+1)*WSIZE:words*WSIZE;

    if((long)(bp=mem_sbrk(size))==-1)return NULL;

    if(words<0)
    {
        size=-size;
        memset(bp-size,0,size+WSIZE);/* clear remain content and 0x1*/
        PUT(bp-size,1);
    }
    else
    {
        SET_HF(bp,size,1);
        PUT(NEXT_BLKP(bp),PACK(0,1));
    }
    return bp;
}

/*
 * find_fit - find an appropriate chunk to return.
 */
static void *find_fit(size_t size)
{
    char *current=NULL;    
    
    current=search4list(size);
    // if(!current&&size>MAX_FASTBIN_SIZE)
    if(!current&&size>0x2000)
    {
        if(has_fastbins(free_pointer_list))
        {
            consolidate();
            current=search4list(size);
        }
    }
    return current;
}

static void *search4list(int size)
{
    char *current,*FD,*BK;
    int idx;
    if(size<=MAX_FASTBIN_SIZE)
    {
        idx=size>>3;
        current=free_pointer_list[idx];
        if(current!=NULL)
        {
            unlink(current,BK,FD);
            return current;
        }
    }
    else
    {
        char **unsorted;

        for(idx=1;idx<=5;idx++)
        {
            if(idx!=5)unsorted=&free_pointer_list[MAX_FASTBIN_COUNT+idx];
            else unsorted=&free_pointer_list[0];
            current=*unsorted;
            while(current!=NULL)
            {           
                // split to 2 chunks            
            #if debug
                printf("size :%d\n",GET_SIZE(current));
            #endif
                if(GET_SIZE(current)>=size+2*DSIZE)
                {
                    unlink(current,BK,FD);
                    SET_HF(current+size,GET_SIZE(current)-size,0);
                    SET_HF(current,size,1);
                    insert2bins(current+size,GET_SIZE(current+size));
                    return current;              
                }
                else if(GET_SIZE(current)>=size)
                {                
                    unlink(current,BK,FD);
                    return current;
                }
                else
                {
                    current=(char*)GET_PREV(current);
                }            
            }        
        }
    }        
    return NULL; 
}

/*
 * consolidate - merge fastbin
 */
void consolidate()
{
    clear_inuse_fastbins(free_pointer_list);

    char *p,*nextchunk,*prevchunk,*FD,*BK,**binhead,*pp;
    int idx=1,size;

    /*consolidate fastbin to the first unsorted bin then consolidate the first bin*/
    do
    {
        binhead=&free_pointer_list[idx];
        p=*binhead;
        while(p)
        {
        #if unlink_debug
            printf("consolidate chunk: %p, \n",p);
            printf("size: 0x%x \n",GET_SIZE(p));
        #endif  
            pp=p;
            prevchunk=PREV_BLKP(p);
        #if unlink_debug    
            printf("p: %p prev: %p prevsize: 0x%x\n",p,prevchunk,GET_SIZE(p-WSIZE));
        #endif
            nextchunk=NEXT_BLKP(p);
            size=GET_SIZE(p);
            // if(!GET_ALLOC(prevchunk))
            if(!GET_ALLOC(p-WSIZE)&&GET_SIZE(p-WSIZE))
            {
                p=prevchunk;
                size+=GET_SIZE(prevchunk);
                unlink(prevchunk,BK,FD);
            #if unlink_debug
                printf("prev success\n");
            #endif
            }
            if(!GET_ALLOC(nextchunk)&&GET_SIZE(nextchunk))
            {
                size+=GET_SIZE(nextchunk);
                unlink(nextchunk,BK,FD);
            #if unlink_debug
                printf("after success\n");
            #endif
            }
            SET_HF(p,size,!alloc);
            unlink(pp,BK,FD);
            
            if(idx!=0)insert2unsorted(p);
            else insert2bins(p,size);
        
        #if unlink_debug
            printf("consolidate chunk: %p, size: 0x%x successfully\n",p,GET_SIZE(p));
            showbins();
        #endif 
            p=*binhead;
             
        }
        idx=(idx+1)%(MAX_FASTBIN_COUNT+UNSORTED_COUNT+1);
    }while(idx!=0);
}

void showbins()
{
    int idx,timesout;
    char *p;
    for(idx=0;idx<=MAX_FASTBIN_COUNT+UNSORTED_COUNT;idx++)
    {
        timesout=10;
        p=free_pointer_list[idx];
        if(idx==0)printf("(>0x3000)idx [%d]:",idx);
        else if(idx<=MAX_FASTBIN_COUNT)printf("(0x%x)idx [%d]:",idx<<3,idx);
        else 
        {
            switch (idx-MAX_FASTBIN_COUNT)
            {
                case 1:
                    printf("(0x60-0x200)idx [%d]:",idx);
                    break;
                case 2:
                    printf("(0x210-0x400)idx [%d]:",idx);
                    break;
                case 3:
                    printf("(0x410-0x1000)idx [%d]:",idx);
                    break;
                case 4:
                    printf("(0x1008-0x3000)idx [%d]:",idx);
                    break;
                
                default:
                    break;
            }
        }
        while(p)
        {
            if(timesout<=0)
            {
                break;
            }
            else timesout--;
            printf("%p (0x%x) ->",p,GET_SIZE(p));
            p=(char*)GET_PREV(p);
        }
        printf("\n");
    }
    printf("\e[32;49;1m-----------------show finish------------------\n\e[39;49;0m");
}