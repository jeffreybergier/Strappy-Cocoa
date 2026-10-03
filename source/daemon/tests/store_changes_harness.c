/* Test the production journal directly without timing-dependent socket races. */
#include "../../shared/strappy_store_client.c"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint64_t first, next, other;
    long long requests[STRAPPY_STORE_MAX_CHANGES];
    int i;
    strappy_store_record_change(STRAPPY_STORE_ALL,0,0);
    assert(strappy_store_client_changes(1,0,&first,requests)==-1);
    strappy_store_record_change(STRAPPY_STORE_LEDGER,1,100);
    strappy_store_record_change(STRAPPY_STORE_LEDGER,1,100);
    strappy_store_record_change(STRAPPY_STORE_RUN,2,0);
    assert(strappy_store_client_changes(1,first,&next,requests)==1 && requests[0]==100);
    assert(strappy_store_client_changes(2,first,&other,requests)==0 && other==next);
    /* Each window retains its own watermark until it applies the result. */
    assert(strappy_store_client_changes(1,first,&other,requests)==1 && other==next);
    strappy_store_record_change(STRAPPY_STORE_LEDGER,2,0);
    assert(strappy_store_client_changes(1,next,&other,requests)==0);
    assert(strappy_store_client_changes(2,next,&other,requests)==-1);
    next=other;
    strappy_store_record_change(STRAPPY_STORE_ALL,0,0);
    assert(strappy_store_client_changes(1,next,&other,requests)==-1);
    assert(strappy_store_client_changes(2,next,&other,requests)==-1);
    next=other;
    for (i=0;i<65;++i) strappy_store_record_change(STRAPPY_STORE_LEDGER,1,100+i);
    assert(strappy_store_client_changes(1,next,&other,requests)==-1);
    next=other;
    for (i=0;i<257;++i) strappy_store_record_change(STRAPPY_STORE_RUN,1,0);
    assert(strappy_store_client_changes(1,next,&other,requests)==-1);
    assert(strappy_store_client_changes(1,other+1,&next,requests)==-1);
    puts("store changes: isolation, deduplication, independent cursors, missing detail, reconnect and bounded overflow passed");
    return 0;
}
