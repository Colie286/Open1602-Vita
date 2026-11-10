#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Project existing locale entries into a heap-free C table for the early launcher."""
import json,pathlib,sys
source=pathlib.Path(sys.argv[1]);output=pathlib.Path(sys.argv[2])
def read(code):
 result={}
 for line in (source/(code+'.lang')).read_text().splitlines():
  if line.startswith('vita.'):
   key,value=line.split('=',1);key,value=key.strip(),value.strip()
   # All shipped Vita text must fit SDL's verified Latin-1 font. Refuse a
   # future unsupported translation with a named build error, never silently
   # drop its characters through the original Windows-1252 conversion.
   for character in value:
    if not (32<=ord(character)<=126 or 160<=ord(character)<=255):
     raise SystemExit(f'{code}:{key}: unsupported Vita font character U+{ord(character):04X}')
   if key.startswith('vita.boot.'):
    if key!='vita.boot.sdl_ready' and not value.isascii():
     raise SystemExit(f'{code}:{key}: early boot font requires ASCII')
    result[key]=value
 return result
en,de=read('en'),read('de');assert en and set(en)==set(de)
rows=[]
for key in sorted(en):
 assert en[key] and de[key]
 rows.append('    {'+', '.join(json.dumps(s,ensure_ascii=False) for s in [key,en[key],de[key]])+'},')
output.write_text('// Generated from data/locale; do not edit.\nstatic const struct {const char *key,*en,*de;} boot_locale[]={\n'+'\n'.join(rows)+'\n};\n')
