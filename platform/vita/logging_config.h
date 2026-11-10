// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared with the pre-newlib starter: bounded streaming parser, no libc/heap.
struct VitaLoggingConfig { char line[512]; unsigned used; int overflow, enabled; };
static inline int vita_logging_equal(const char* a, unsigned n, const char* b) {
    unsigned i=0;while(i<n&&b[i]&&a[i]==b[i])++i;return i==n&&!b[i];
}
static inline void vita_logging_line(struct VitaLoggingConfig* s) {
    unsigned a=0,b=s->used;
    if(s->overflow)return;
    if(b>=3&&(unsigned char)s->line[0]==239&&(unsigned char)s->line[1]==187&&(unsigned char)s->line[2]==191)a=3;
    while(a<b&&(s->line[a]==' '||s->line[a]=='\t'))++a;
    unsigned key=a;while(a<b&&s->line[a]!='=')++a;if(a==b)return;
    unsigned end=a;while(end>key&&(s->line[end-1]==' '||s->line[end-1]=='\t'))--end;
    if(!vita_logging_equal(s->line+key,end-key,"logging"))return;
    ++a;while(a<b&&(s->line[a]==' '||s->line[a]=='\t'))++a;
    while(b>a&&(s->line[b-1]==' '||s->line[b-1]=='\t'||s->line[b-1]=='\r'))--b;
    if(vita_logging_equal(s->line+a,b-a,"on")||vita_logging_equal(s->line+a,b-a,"true")||vita_logging_equal(s->line+a,b-a,"1"))s->enabled=1;
    if(vita_logging_equal(s->line+a,b-a,"off")||vita_logging_equal(s->line+a,b-a,"false")||vita_logging_equal(s->line+a,b-a,"0"))s->enabled=0;
}
static inline void vita_logging_byte(struct VitaLoggingConfig* s,char c) {
    if(c=='\n'){vita_logging_line(s);s->used=0;s->overflow=0;}
    else if(s->used<sizeof(s->line))s->line[s->used++]=c;
    else s->overflow=1;
}
