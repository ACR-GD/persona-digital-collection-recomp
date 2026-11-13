import sys, os, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import *
from collections import Counter
tot=Counter()
for p in sorted(glob.glob(sys.argv[1]+'/*.dxr'))+[sys.argv[1]+'/chara.cxt']:
    f=DirFile(p)
    c=Counter(); 
    for n,ch in f.cast_list():
        m=Member(n,ch); c[m.tname]+=1
    nstxt=len(f.by_tag.get('STXT',[]))
    lits=0; jl=0
    for ch in f.by_tag.get('Lscr',[]):
        try:
            for t,v,_ in parse_lscr_literals(ch.data):
                if t==1:
                    lits+=1
                    if any(b>=0x80 for b in v): jl+=1
        except Exception as e: print('lscr err',e)
    tags=Counter(ch.tag for ch in f.chunks)
    print('%-12s %s STXT=%d strlits=%d jp=%d other=%s'%(os.path.basename(p),dict(c),nstxt,lits,jl,{k:v for k,v in tags.items() if k not in('free','CASt','BITD','Lscr','THUM','STXT')}))
