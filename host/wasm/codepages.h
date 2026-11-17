/*
 * The character code pages (codepages.c): what MultiByteToWideChar and WideCharToMultiByte convert
 * with, in both tiers — kernel32.c's fast entries call these directly, and win32/shim.js calls the
 * same functions as exports of the module, so each table exists once.
 */
#ifndef HOSTFN_CODEPAGES_H
#define HOSTFN_CODEPAGES_H

#include "layout.h"

#define CP_UTF8               65001u
#define WC_NO_BEST_FIT_CHARS  0x400u

/* Characters first..last take bytes byte, byte + 1, ... */
typedef struct { unsigned short first, last; unsigned char byte; } EncodeRun;

/* Code page 20127's tables (codepage20127.c): bytes 0x80..0xff, and the sorted, disjoint runs. */
#define CP20127_RUNS 274u
extern const unsigned short CP20127_DECODE[128];
extern const EncodeRun CP20127_ENCODE[CP20127_RUNS];

/* The code page `cp` names here — 1252, 437 or CP_UTF8 — or 0 for one this host has no table for. */
u32 codePageOf(u32 cp);
/* The character byte `b` is under `page` (1252 or 437). */
u32 codePageDecode(u32 page, u32 b);
/* The byte character `ch` becomes under `page` (1252 or 437), or -1 for one the page has no byte for. */
i32 codePageEncode(u32 page, u32 ch, u32 flags);

#endif
