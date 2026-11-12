#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Require a separate title and a package containing executable, metadata, licenses and explicitly allowed LiveArea art."""
import hashlib, pathlib, struct, sys, zipfile, io, xml.etree.ElementTree as ET
from PIL import Image
path=pathlib.Path(sys.argv[1])
qa="--qa" in sys.argv[2:]
with zipfile.ZipFile(path) as z:
    assert z.testzip() is None
    template=ET.fromstring(z.read('sce_sys/livearea/contents/template.xml')) if 'sce_sys/livearea/contents/template.xml' in z.namelist() else None
    custom_launch=template is not None and template.get('style')=='ad0'
    art={'sce_sys/icon0.png':(128,128),'sce_sys/livearea/contents/bg0.png':(840,500),'sce_sys/livearea/contents/startup.png':((560,188) if custom_launch else (280,158))}
    extra=set(art)|{'sce_sys/livearea/contents/template.xml'}
    assert set(z.namelist()) <= extra | {'eboot.bin','client.self','sce_sys/param.sfo','licenses/Open1602.txt','licenses/enet.txt','licenses/lua.txt','licenses/SDL3.txt','licenses/zlib.txt','licenses/ThirdParty.txt','licenses/dav1d.txt','licenses/opus.txt'}
    b=z.read('sce_sys/param.sfo')
    magic,version,keys,values,count=struct.unpack_from('<5I',b)
    assert magic==0x46535000
    entries={}
    for i in range(count):
        key,fmt,length,maximum,value=struct.unpack_from('<HHIII',b,20+i*16)
        name=b[keys+key:].split(b'\0')[0].decode()
        entries[name]=b[values+value:values+value+length].rstrip(b'\0')
    assert entries['TITLE_ID'] in (b'O1602VITA',b'O1602NET8'), entries
    app_version=entries.get('APP_VER',b'').decode()
    if app_version>='00.03':
        assert 'client.self' in z.namelist(), 'hardware test must retain the full client'
    if app_version>='01.00':
        brand=b'Open1602' if app_version>='01.50' else b'Anno 1602'
        assert entries['TITLE']==brand+(b' Worker QA' if qa else b'')
        assert extra<=set(z.namelist())
        for name,size in art.items():
            raw=z.read(name);assert len(raw)<=420*1024
            im=Image.open(io.BytesIO(raw));assert im.size==size and im.mode=='P',name
            assert len(im.getcolors() or [])<=128,name
        xml=z.read('sce_sys/livearea/contents/template.xml');assert len(xml)<32768
        template=ET.fromstring(xml)
        if custom_launch:
            assert template.find('gate') is None
            assert len(template.findall('frame'))==1
            assert template.find('frame').get('id')=='frame1'
            item=template.find('frame/liveitem')
            assert item is not None and item.findtext('target')=='psla:eboot'
            assert item.findtext('image')=='startup.png'
            assert item.find('image').attrib==dict(align='center',valign='center',origin='frame',width='560',height='188',x='0',y='43')
            assert len(template.findall('.//target'))==1
        else:
            assert template.findtext('gate/startup-image')=='startup.png'
            assert template.find('.//target') is None
        assert template.findtext('livearea-background/image')=='bg0.png'
    if app_version>='01.50':
        source=pathlib.Path(__file__).resolve().parents[2]
        for member,filename in [('sce_sys/icon0.png','icon0.png'),('sce_sys/livearea/contents/bg0.png','bg0.png'),('sce_sys/livearea/contents/startup.png','startup.png')]:
            assert z.read(member)==(source/'platform/vita/livearea/assets'/filename).read_bytes(), 'unexpected current LiveArea artwork'
        assert z.read('licenses/ThirdParty.txt')==(source/'platform/vita/licenses/ThirdParty.txt').read_bytes()
    assert z.read('eboot.bin')[:4]==b'SCE\0'
    if 'client.self' in z.namelist():assert z.read('client.self')[:4]==b'SCE\0'
print('PACKAGE PASS title='+entries['TITLE_ID'].decode()+' game_data=0 artwork='+str(len(set(z.namelist())&set(art)))+' sha256='+hashlib.sha256(path.read_bytes()).hexdigest())
