#ifndef SC_KEY_RECORD_H
#define SC_KEY_RECORD_H

/* A bounded recording that uses the existing macro format. Space for every
   outstanding release is reserved before accepting a key-down event. */
typedef struct {
    char sequence[SC_KEYS_CAP];
    BYTE held[256], extended[256];
    size_t length, releaseSpace;
    DWORD lastTime, totalDelay;
    int hasEvent;
} KeyRecording;

static void KeyRecord_Reset(KeyRecording *recording)
{ memset(recording,0,sizeof(*recording)); }

static int KeyRecord_Token(char *token, UINT vk, int extended, int up)
{ return snprintf(token,32,"{vk%02X%s %s}",vk,extended?"E":"",up?"up":"down"); }

/* 1: accepted/ignored repeat; 0: full. The caller finishes on a full buffer. */
static int KeyRecord_Event(KeyRecording *r, UINT vk, int extended, int up, DWORD time)
{
    if (!vk || vk >= 256 || (up ? !r->held[vk] : r->held[vk])) return 1;
    char token[32], release[32], delay[256]="";
    if (up) extended=r->extended[vk];
    int tokenLength=KeyRecord_Token(token,vk,extended,up);
    int releaseLength=KeyRecord_Token(release,vk,extended,1);
    size_t reserved=up?r->releaseSpace-releaseLength:r->releaseSpace+releaseLength;
    DWORD elapsed=r->hasEvent?time-r->lastTime:0;
    if (elapsed>60000-r->totalDelay) return 0;
    /* A single playback delay is limited to five seconds. */
    for (DWORD remaining=elapsed;remaining;) {
        DWORD part=remaining>5000?5000:remaining;
        char chunk[32];snprintf(chunk,sizeof(chunk),"{Sleep %lu}",(unsigned long)part);
        strcat(delay,chunk);remaining-=part;
    }
    size_t delayLength=strlen(delay);
    if (r->length+delayLength+tokenLength+reserved>=sizeof(r->sequence)) return 0;
    memcpy(r->sequence+r->length,delay,delayLength);r->length+=delayLength;
    memcpy(r->sequence+r->length,token,tokenLength+1);r->length+=tokenLength;
    r->held[vk]=!up;r->extended[vk]=(BYTE)extended;r->releaseSpace=reserved;
    r->totalDelay+=elapsed;r->lastTime=time;r->hasEvent=1;
    return 1;
}

static void KeyRecord_Finish(KeyRecording *r)
{
    for (UINT vk=1;vk<256;vk++) if (r->held[vk]) {
        char token[32];int length=KeyRecord_Token(token,vk,r->extended[vk],1);
        memcpy(r->sequence+r->length,token,length+1);r->length+=length;r->held[vk]=0;
    }
    r->releaseSpace=0;
}

#endif
