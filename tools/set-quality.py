#!/usr/bin/env python3
# usage: set-quality.py <attributes.xml> key=value [key=value ...]   (keys are the SysSpec* attribute names)
import sys, re
p = sys.argv[1]; s = open(p, encoding='utf-8', errors='replace').read(); changed = []
for kv in sys.argv[2:]:
    k, v = kv.split('=')
    pat = re.compile(r'(<Attr\s+name="' + re.escape(k) + r'"\s+value=")([^"]*)(")')
    if pat.search(s):
        old = pat.search(s).group(2); s = pat.sub(lambda m: m.group(1) + v + m.group(3), s, count=1); changed.append(f"{k}: {old}->{v}")
    else: changed.append(f"{k}: MISSING")
open(p, 'w', encoding='utf-8').write(s); print('; '.join(changed))
