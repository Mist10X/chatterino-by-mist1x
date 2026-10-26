"""One-time, audited migration of the inventory's explicit UI literal sites.

This consumes the pre-migration manifest. It does not perform a global string
replacement and intentionally leaves dropdown values and already translated sites alone.
"""
import collections
import json
from pathlib import Path
import re
from inventory import scan

ROOT = Path(__file__).resolve().parents[2]
inventory = json.loads((Path(__file__).parent / 'inventory.json').read_text('utf-8'))
files = collections.defaultdict(dict)
for filename in sorted({entry['file'] for places in inventory.values() for entry in places}):
    for entry in scan(ROOT / filename):
        if entry['already'] or not entry['rewrite']:continue
        files[entry['file']][(entry['start'], entry['end'])] = entry
changed = []
for name, spans in sorted(files.items()):
    path = ROOT / name
    source = path.read_text('utf-8-sig')
    for (start, end), entry in sorted(spans.items(), reverse=True):
        original = source[start:end]
        source = source[:start] + 'uiText(' + original + ')' + source[end:]
    include = '#include "common/UiLanguage.hpp"'
    if include not in source:
        match = re.search(r'#\s*include\b', source)
        first = match.start() if match else 0
        source = source[:first] + include + '\n' + source[first:]
    path.write_text(source, 'utf-8')
    changed.append(dict(file=name, sites=len(spans)))
(Path(__file__).parent / 'migrated-sites.json').write_text(json.dumps(changed, indent=2), 'utf-8')
print(f'{sum(x["sites"] for x in changed)} explicit UI literals in {len(changed)} files')
