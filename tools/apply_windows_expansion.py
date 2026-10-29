#!/usr/bin/env python3
"""One-time migration; resulting ordinary source files are committed before build."""
from pathlib import Path
import base64, hashlib, json, zlib
raw = zlib.decompress(base64.b64decode(Path(__file__).with_suffix('.b64').read_text()))
assert hashlib.sha256(raw).hexdigest() == '7d612fc3b9ae17c16bc0d32e916f73efaa72047c953a82ff8bb03a2e2ed66f1d'
spec = json.loads(raw)
root = Path(__file__).resolve().parents[1]
def function_bounds(text, signature):
    if text.count(signature) != 1: raise RuntimeError('Nonunique function: '+signature)
    a=text.index(signature); j=text.index('{',a); depth=0; state='code'
    while j<len(text):
        c=text[j]; pair=text[j:j+2]
        if state=='line':
            if c=='\n': state='code'
        elif state=='block':
            if pair=='*/': state='code'; j+=1
        elif state in ('"', "'"):
            if c=='\\': j+=1
            elif c==state: state='code'
        elif pair=='//': state='line'; j+=1
        elif pair=='/*': state='block'; j+=1
        elif c in ('"', "'"): state=c
        elif c=='{': depth+=1
        elif c=='}':
            depth-=1
            if not depth: return a,j+1
        j+=1
    raise RuntimeError('Unclosed function: '+signature)
changed={}
for edit in spec['replacements']:
    name=edit['path']; p=root/name
    if not p.resolve().is_relative_to(root): raise RuntimeError('Unsafe path')
    text=changed[name] if name in changed else p.read_text()
    if 'append' in edit: text+=edit['append']
    elif 'signature' in edit:
        a,b=function_bounds(text,edit['signature']); text=text[:a]+edit['new']+text[b:]
    else:
        if text.count(edit['old'])!=1: raise RuntimeError('Source differs at '+name+': '+edit['old'][:120])
        text=text.replace(edit['old'],edit['new'],1)
    changed[name]=text
for name,text in spec['new_files'].items():
    if (root/name).exists(): raise RuntimeError('New file already exists: '+name)
    changed[name]=text
for name,text in changed.items():
    p=root/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(text)
print('Applied Windows expansion to',len(changed),'source/test/doc files')
Path(__file__).with_suffix('.b64').unlink()
Path(__file__).unlink()
