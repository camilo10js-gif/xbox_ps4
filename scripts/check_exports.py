from pathlib import Path
import base64,hashlib,struct
import sys
p=Path(sys.argv[1])
b=p.read_bytes(); phoff=struct.unpack_from('<Q',b,32)[0]; size,count=struct.unpack_from('<HH',b,54)
ph=[struct.unpack_from('<IIQQQQQQ',b,phoff+i*size) for i in range(count)]
dyn=next(x for x in ph if x[0]==2); sce=next(x for x in ph if x[0]==0x61000000)
tags=dict(struct.unpack_from('<QQ',b,dyn[2]+i*16) for i in range(dyn[5]//16))
strtab=sce[2]+tags[0x61000035]; symtab=sce[2]+tags[0x61000039]; symsize=tags[0x6100003f]
symbols={}
for i in range(symsize//24):
 name,info,other,section,value,sz=struct.unpack_from('<IBBHQQ',b,symtab+i*24)
 end=b.index(b'\0',strtab+name); name=b[strtab+name:end].decode()
 symbols[name]=(info,other,section,value,sz)
for name in ['plugin_load','plugin_unload','g_pluginName']:
 digest=hashlib.sha1(name.encode()+bytes.fromhex('518D64A635DED8C1E6B039B1C3E55230')).digest()
 nid=base64.b64encode(digest[:8][::-1]).decode().rstrip('=').replace('/','-')
 matches=[v for k,v in symbols.items() if k.startswith(nid+'#')]
 assert len(matches)==1,(name,nid,matches)
 info,other,section,value,sz=matches[0]
 assert info>>4==1 and other&3==0 and section!=0 and value!=0,(name,matches)
 print('PASS PS4 export:',name,nid,hex(value))
