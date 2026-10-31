/* Offline name keys shared by alphabetical sorting and phonetic search. */
#ifndef SC_PINYIN_H
#define SC_PINYIN_H
#include "pinyin_data.h"
#define SC_PINYIN_CAP (SC_NAME_CAP*6)
typedef struct {
    wchar_t name[SC_NAME_CAP],full[SC_PINYIN_CAP],initials[SC_NAME_CAP];
} SC_PinyinKey;

static const char *SC_PinyinCharacter(unsigned int code)
{
    size_t lo=0,hi=sizeof(sc_pinyinCharacters)/sizeof(sc_pinyinCharacters[0]);
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;
        if(sc_pinyinCharacters[mid].code<code)lo=mid+1;
        else if(sc_pinyinCharacters[mid].code>code)hi=mid;
        else return sc_pinyinSyllables[sc_pinyinCharacters[mid].syllable];
    }
    return NULL;
}
static int SC_PinyinPhrase(const wchar_t *text,size_t length)
{
    wchar_t phrase[SC_PINYIN_PHRASE_MAX+1];
    wmemcpy(phrase,text,length);phrase[length]=0;
    size_t lo=0,hi=sizeof(sc_pinyinPhrases)/sizeof(sc_pinyinPhrases[0]);
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;int comparison=wcscmp(sc_pinyinPhrases[mid].text,phrase);
        if(comparison<0)lo=mid+1;
        else if(comparison>0)hi=mid;
        else return (int)mid;
    }
    return -1;
}
static void SC_PinyinAppend(wchar_t *output,size_t capacity,size_t *used,const char *text)
{
    while(*text && *used+1<capacity)output[(*used)++]=(unsigned char)*text++;
    output[*used]=0;
}
static void SC_PinyinBuild(SC_PinyinKey *key,const wchar_t *name)
{
    lstrcpynW(key->name,name,SC_NAME_CAP);
    size_t count=wcslen(key->name),full=0,initials=0;
    key->full[0]=key->initials[0]=0;
    for(size_t i=0;i<count;){
        size_t length=count-i;if(length>SC_PINYIN_PHRASE_MAX)length=SC_PINYIN_PHRASE_MAX;
        int phrase=-1;
        /* Only Han characters need a phrase lookup. */
        if(SC_PinyinCharacter(key->name[i]))
            for(;length>1;length--)if((phrase=SC_PinyinPhrase(key->name+i,length))>=0)break;
        if(phrase>=0){
            SC_PinyinAppend(key->full,SC_PINYIN_CAP,&full,sc_pinyinPhrases[phrase].full);
            SC_PinyinAppend(key->initials,SC_NAME_CAP,&initials,sc_pinyinPhrases[phrase].initials);
            i+=length;continue;
        }
        unsigned int code=key->name[i];size_t units=1;
        if(code>=0xd800 && code<=0xdbff && i+1<count && key->name[i+1]>=0xdc00 && key->name[i+1]<=0xdfff){
            code=0x10000+((code-0xd800)<<10)+(key->name[i+1]-0xdc00);units=2;
        }
        const char *syllable=SC_PinyinCharacter(code);
        if(syllable){
            SC_PinyinAppend(key->full,SC_PINYIN_CAP,&full,syllable);
            if(initials+1<SC_NAME_CAP)key->initials[initials++]=(unsigned char)syllable[0];
        }else for(size_t u=0;u<units;u++){
            wchar_t ch=key->name[i+u];
            if(full+1<SC_PINYIN_CAP)key->full[full++]=ch;
            if(initials+1<SC_NAME_CAP)key->initials[initials++]=ch;
        }
        key->full[full]=key->initials[initials]=0;i+=units;
    }
}
#endif
