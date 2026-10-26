"""Inventory explicit UI text sites; never inspect profiles or translate chat data.

String literals are lexed before calls, so comments, raw strings and quoted punctuation
cannot be mistaken for code. Only display arguments of named UI APIs are candidates.
"""
import argparse
import collections
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?\'(?:\\.|[^\'\\])*\'|(?:u8|u|U|L)?R"(?P<d>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=d)"|(?:u8|u|U|L)?"(?:\\.|[^"\\])*"(?:_[A-Za-z0-9]+)?|[A-Za-z_][A-Za-z_0-9]*|[^\s]', re.M)
STR = re.compile(r'^(?:u8|u|U|L)?"')
FIRST = {'setText', 'setToolTip', 'setTooltip', 'setWindowTitle', 'setPlaceholderText', 'setStatusTip',
         'setInformativeText', 'setDetailedText', 'makeFieldLabel', 'makeHelperLabel',
         'updateBotBadgeStatus', 'setStatusMessage', 'setError', 'showInfo',
         'setStatus', 'showStatus', 'setStatusText', 'setFeedback', 'setHelpText',
         'setInfoText', 'showErrorMessage', 'setValidationMessage', 'updateHint',
         'setHint', 'makeHeaderLabel', 'makeStatusLabel', 'showEmpty',
         'setAccessibleName', 'setAccessibleDescription', 'setWhatsThis', 'addAction', 'addMenu',
         'addButton', 'addTitle', 'addSubtitle', 'addDescription', 'setDescription', 'setLabelText',
         'setSuffix', 'setPrefix', 'setTitle', 'setTitles', 'showEditError', 'showError', 'setHeaderLabels', 'setHorizontalHeaderLabels',
         'setVerticalHeaderLabels', 'addString', 'appendString'}
CTOR = {'QLabel', 'QPushButton', 'QCheckBox', 'QRadioButton', 'QGroupBox', 'QAction',
        'SLabel', 'SPushButton', 'SCheckBox', 'TitleLabel', 'DescriptionLabel', 'SubtitleLabel'}
FACTORIES = {'checkbox', 'inverseCheckbox', 'customCheckbox', 'intInput', 'dropdown', 'lineEdit',
             'colorButton', 'createCheckBox', 'createComboBox', 'addDropdown', 'addCheckbox', 'addLabel', 'addCheckBox'}
WRAPPERS = {'QString', 'QStringLiteral', 'QLatin1String', 'QLatin1StringView', 'fromUtf8', 'fromLatin1', 'tr', 'uiText', 'QT_TRANSLATE_NOOP'}

def string_value(raw):
    raw = re.sub(r'^(u8|u|U|L)', '', raw)
    if raw.startswith('R"'):
        left = raw.index('(');return raw[left + 1:raw.rindex(')')]
    raw = re.sub(r'_[A-Za-z0-9]+$', '', raw)
    # Preserve Unicode; JSON decodes the ordinary escapes used by these UI literals.
    try:return json.loads(raw)
    except ValueError:
        return re.sub(r'\\([\'"?])', r'\1', raw[1:-1]).replace('\\n', '\n').replace('\\t', '\t')

def scan(path):
    source = path.read_text(encoding='utf-8-sig')
    tokens = [(m.group(), m.start(), m.end()) for m in TOKEN.finditer(source)
              if not m.group().startswith(('//', '/*'))]
    stack = [];out = []
    for i, (tok, start, end) in enumerate(tokens):
        if tok == '(':
            j = i - 1
            if j >= 0 and tokens[j][0] == '>':
                depth = 1;j -= 1
                while j >= 0 and depth:
                    if tokens[j][0] == '>':depth += 1
                    elif tokens[j][0] == '<':depth -= 1
                    j -= 1
            name = tokens[j][0] if j >= 0 else ''
            # emplace<QLabel> is another explicit display-text constructor.
            if name == 'emplace' and any(t[0] in CTOR for t in tokens[j:i]):name = 'QLabel'
            stack.append([name, 0, i])
        elif tok in ('{', '['):stack.append([tok, 0, i])
        elif tok in (')', '}', ']'):
            if stack:stack.pop()
        elif tok == ',':
            if stack:stack[-1][1] += 1
        elif STR.match(tok) or re.match(r'^(?:u8|u|U|L)?R"', tok):
            if i and (STR.match(tokens[i - 1][0]) or re.match(r'^(?:u8|u|U|L)?R"', tokens[i - 1][0])):continue
            atoms = [tok];last = i
            while last + 1 < len(tokens) and (STR.match(tokens[last + 1][0]) or re.match(r'^(?:u8|u|U|L)?R"', tokens[last + 1][0])):
                last += 1;atoms.append(tokens[last][0])
            calls = [c for c in stack if c[0] not in WRAPPERS and c[0] not in ('{', '[')]
            if not calls:continue
            name, arg, call_i = calls[-1]
            selected = ((name in FIRST | CTOR | FACTORIES and arg == 0)
                        or (name in {'warning', 'information', 'critical', 'question', 'getText', 'getItem',
                                     'getOpenFileName', 'getSaveFileName', 'getExistingDirectory'} and arg in (1, 2))
                        or (name in {'addTab', 'insertTab'} and arg in (0, 1, 2))
                        or name in {'setHeaderLabels', 'setHorizontalHeaderLabels'}
                        or (name == 'addDropdown' and arg == 1))
            selected |= name == 'appendTab' and arg == 1
            selected |= name in {'QAction', 'QPushButton', 'addAction'} and arg == 1
            selected |= name in {'setLabelStatus', 'showText', 'setButtonText'} and arg == 1
            if not selected:continue
            text = ''.join(string_value(atom) for atom in atoms)
            if not re.search('[A-Za-z]{2}', text) or re.search('[А-Яа-яЁё]', text):continue
            if text.startswith((':/', 'http://', 'https://', 'color:', 'background:', 'font-', '<svg')):continue
            # Resource paths, shortcut sequences and command syntax are not translations.
            if re.fullmatch(r'[A-Za-z0-9_./:*+\-]+', text) and (text.startswith('/') or '/' in text or '::' in text):continue
            atom_start, atom_end = start, tokens[last][2]
            before, after = i - 1, last + 1
            while before >= 1 and after < len(tokens) and tokens[before][0] == '(' and tokens[after][0] == ')' and tokens[before - 1][0] in WRAPPERS:
                begin = before - 1
                if begin >= 2 and tokens[begin - 1][0] == ':' and tokens[begin - 2][0] == ':':begin -= 3
                atom_start, atom_end = tokens[begin][1], tokens[after][2]
                before, after = begin - 1, after + 1
            out.append(dict(text=text, file=path.relative_to(ROOT).as_posix(), line=source.count('\n', 0, start) + 1,
                            api=name, arg=arg, start=atom_start, end=atom_end,
                            already=any(c[0] in {'tr', 'uiText', 'QT_TRANSLATE_NOOP'} for c in stack),
                            rewrite=not (name == 'addDropdown' and arg == 1)))
    return out

def main():
    entries = [item for path in sorted((ROOT / 'src').rglob('*.cpp')) for item in scan(path)]
    entries += [item for path in sorted((ROOT / 'src/widgets').rglob('*.hpp')) for item in scan(path)]
    grouped = collections.defaultdict(list)
    for item in entries:grouped[item['text']].append(item)
    dest = Path(__file__).parent / 'inventory.json'
    dest.write_text(json.dumps(dict(sorted(grouped.items())), ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'{len(entries)} UI sites; {len(grouped)} unique literals; {dest}')
    for text, places in grouped.items():print(json.dumps(text, ensure_ascii=False))

if __name__ == '__main__':main()
