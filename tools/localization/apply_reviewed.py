"""Resolve reviewed UI formatting literals before .arg(), without touching preset schemas.

Only QString-producing atoms are eligible. Bare strings (const char* fields,
protocol names, command arguments), comparisons, static descriptors and setting
value factories stay unchanged. Their UI consumers translate display labels.
"""
import json
from pathlib import Path
import re
from inventory import ROOT, TOKEN, STR, string_value

HERE=Path(__file__).parent
review=list(json.loads((HERE/'review-needed.json').read_text('utf-8')))
approved=set()
for p in HERE.glob('review-*.tsv'):
    approved.update(review[int(line.split('\t',1)[0])] for line in p.read_text('utf-8').splitlines() if line)
changes=[]
for path in sorted((ROOT/'src/widgets').rglob('*.cpp')):
    if path.name in {'GeneralPage.cpp','MoltorinoPage.cpp','ChannelFilterEditorDialog.cpp'}:continue
    source=path.read_text('utf-8')
    tokens=[m for m in TOKEN.finditer(source) if not m[0].startswith(('//','/*'))]
    i=0;spans=[]
    while i<len(tokens):
        if not STR.match(tokens[i][0]):i+=1;continue
        first=i;text=string_value(tokens[i][0]);i+=1
        while i<len(tokens) and STR.match(tokens[i][0]):text+=string_value(tokens[i][0]);i+=1
        if text not in approved:continue
        start,end=tokens[first].start(),tokens[i-1].end()
        if first>=2 and tokens[first-1][0]=='(' and tokens[first-2][0] in {'QString','QStringLiteral'} and i<len(tokens) and tokens[i][0]==')':
            start,end=tokens[first-2].start(),tokens[i].end()
        elif not re.search(r'_(s|qs)$',tokens[i-1][0]):continue
        before=source[max(0,start-80):start]
        if re.search(r'(uiText|tr)\(\s*$',before):continue
        if re.search(r'(==|!=|\.compare\(|\.contains\()\s*$',before):continue
        if re.search(r'\b(static|constexpr)\b',source[source.rfind('\n',0,start)+1:start]):continue
        spans.append((start,end))
    if spans:
        for start,end in sorted(set(spans),reverse=True):source=source[:start]+'uiText('+source[start:end]+')'+source[end:]
        if '#include "common/UiLanguage.hpp"' not in source:
            first=source.index('#include ');source=source[:first]+'#include "common/UiLanguage.hpp"\n'+source[first:]
        path.write_text(source,'utf-8');changes.append({'file':path.relative_to(ROOT).as_posix(),'sites':len(spans)})
(HERE/'reviewed-sites.json').write_text(json.dumps(changes,indent=2),'utf-8')
print(sum(x['sites'] for x in changes),'reviewed UI formatting atoms;',len(changes),'files')
