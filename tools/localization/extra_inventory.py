"""UI-only supplemental sources: headers, .ui forms, option descriptors and existing Russian labels."""
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

from inventory import ROOT, TOKEN, STR, WRAPPERS, scan, string_value

HERE = Path(__file__).parent
base = json.loads((HERE / 'inventory.json').read_text('utf-8'))
english = set()
for path in (ROOT / 'src/widgets').rglob('*.cpp'):
    english.update(entry['text'] for entry in scan(path) if entry['text'] not in base)
for path in (ROOT / 'src/widgets').rglob('*.ui'):
    english.update(e.text for e in ET.parse(path).iter('string') if e.text and re.search('[A-Za-z]', e.text))
for path in [ROOT / 'src/controllers/hotkeys/ActionNames.hpp', ROOT / 'src/singletons/Settings.hpp',
             ROOT / 'src/common/LastMessageLineStyle.hpp', ROOT / 'src/common/ThumbnailPreviewMode.hpp',
             ROOT / 'src/controllers/sound/ISoundController.hpp', ROOT / 'src/common/StreamerModeSetting.hpp',
             ROOT / 'src/providers/emoji/EmojiStyle.hpp']:
    code = path.read_text('utf-8')
    clean = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', code)
    if path.name == 'ActionNames.hpp':
        atoms = [m.group() for m in TOKEN.finditer(code) if not m.group().startswith(('//','/*'))]
        i = 0
        while i < len(atoms):
            if STR.match(atoms[i]):
                value = string_value(atoms[i]);i += 1
                while i < len(atoms) and STR.match(atoms[i]):value += string_value(atoms[i]);i += 1
                if value and (value[0].isupper() or ' ' in value):english.add(value)
            else:i += 1
    else:
        for block in re.findall(r'enum(?: class)?\s+\w+[^{}]*\{([^}]+)\}', clean):
            for value in block.split(','):
                match = re.match(r'\s*([A-Za-z]\w*)', value)
                if match:english.add(match[1])
        english.update(string_value(m[1]) for m in re.finditer(r'return\s+((?:u8|u)?"(?:\\.|[^"\\])*"(?:_\w+)?);', code))
english.update(['Popup Windows', 'Split', 'Split input box', 'System'])

russian = set();changes = []
for path in sorted((ROOT / 'src/widgets').rglob('*.cpp')):
    source = path.read_text('utf-8')
    tokens = [(m.group(), m.start(), m.end()) for m in TOKEN.finditer(source) if not m.group().startswith(('//','/*'))]
    spans = []
    for i, (atom, start, end) in enumerate(tokens):
        if not STR.match(atom):continue
        if i and STR.match(tokens[i-1][0]):continue
        text = string_value(atom);last = i
        while last+1 < len(tokens) and STR.match(tokens[last+1][0]):
            last += 1;text += string_value(tokens[last][0])
        if not re.search('[А-Яа-яЁё]', text):continue
        russian.add(text)
        # Constexpr stringviews are resolved at their display sites. The plural
        # helper keeps its const char* words and has a separate English branch.
        if 'constexpr' in source[source.rfind('\n',0,start)+1:start] or text in {'месяц','месяца','месяцев'}:continue
        before, after = i-1, last+1;atom_start, atom_end = start,tokens[last][2]
        already = False
        while before >= 1 and after < len(tokens) and tokens[before][0] == '(' and tokens[after][0] == ')' and tokens[before-1][0] in WRAPPERS:
            name = tokens[before-1][0]
            if name in {'uiText','tr'}:already=True
            begin=before-1
            atom_start, atom_end=tokens[begin][1],tokens[after][2]
            before,after=begin-1,after+1
        if not already:spans.append((atom_start,atom_end))
    if '--apply-russian' in sys.argv and spans:
        for start,end in sorted(set(spans),reverse=True):source=source[:start]+'uiText('+source[start:end]+')'+source[end:]
        if '#include "common/UiLanguage.hpp"' not in source:
            first=source.index('#include ');source=source[:first]+'#include "common/UiLanguage.hpp"\n'+source[first:]
        path.write_text(source,'utf-8');changes.append(path.relative_to(ROOT).as_posix())

english = sorted(text for text in english if text not in base and text and not re.search('[А-Яа-яЁё]',text))
(HERE/'extra-en-sources.json').write_text(json.dumps(english,ensure_ascii=False,indent=2),'utf-8')
(HERE/'existing-ru-sources.json').write_text(json.dumps(sorted(russian),ensure_ascii=False,indent=2),'utf-8')
print('English extra',len(english),'Russian existing',len(russian),'Changed',changes)
for i,text in enumerate(english):print(f'{i}\t{text}')
