"""Build the reviewable Russian UI catalog; verify formatting and content boundaries."""
import collections
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).parent

def cpp_hex(text):
    # C++ UTF-8 byte escapes occur in a few existing labels (arrows, middots).
    return re.sub(r'(?:\\x[0-9a-fA-F]{2})+', lambda m: bytes.fromhex(m[0].replace('\\x', '')).decode('utf-8'), text)

def build():
    source = list(json.loads((HERE / 'inventory.json').read_text('utf-8')))
    translations = {}
    approved_unchanged = []
    for path in sorted(HERE.glob('ru-*.tsv')):
        for line in path.read_text('utf-8').splitlines():
            if not line or line.startswith('#'):continue
            raw, value = line.split('\t', 1)
            index = int(raw)
            assert index not in translations, (path.name, index, 'duplicate')
            if value == '=':
                value = source[index];approved_unchanged.append(index)
            translations[index] = value.replace('\\n', '\n').replace('\\t', '\t')
    assert set(translations) == set(range(len(source))), ('Missing ids', set(range(len(source))) - translations.keys())
    result = {cpp_hex(s): translations[i] for i, s in enumerate(source)}
    def supplement(source_file, translations_file):
        keys = json.loads((HERE / source_file).read_text('utf-8'))
        entries = {}
        for line in (HERE / translations_file).read_text('utf-8').splitlines():
            if not line or line.startswith('#'):continue
            number, target = line.split('\t', 1)
            key = keys[int(number)]
            assert key not in entries, ('Duplicate supplemental key', key)
            entries[key] = key if target == '=' else target.replace('\\n','\n').replace('\\t','\t')
        assert set(entries) == set(keys), ('Missing supplemental translations', set(keys)-entries.keys())
        return entries
    result.update(supplement('extra-en-sources.json', 'extra-ru.tsv'))
    result.update(supplement('final-en-sources.json', 'final-ru.tsv'))
    english = supplement('existing-ru-sources.json', 'existing-en.tsv')
    branding_path = HERE / 'branding-overrides.json'
    branding = json.loads(branding_path.read_text('utf-8')) if branding_path.exists() else {}
    english.update(branding.get('en', {}))
    english_path = ROOT / 'resources/i18n/en.json'
    english_path.write_text(json.dumps(english, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    result.update({key:key for key in english})
    review_sources = list(json.loads((HERE / 'review-needed.json').read_text('utf-8')))
    for path in sorted(HERE.glob('review-*.tsv')):
        for line in path.read_text('utf-8').splitlines():
            if not line:continue
            number, target = line.split('\t', 1)
            result[cpp_hex(review_sources[int(number)])] = target.replace('\\n','\n').replace('\\t','\t')
    extra_path = HERE / 'extra-ru.json'
    if extra_path.exists():result.update(json.loads(extra_path.read_text('utf-8')))
    result.update(branding.get('ru', {}))
    for key, value in [*result.items(), *english.items()]:
        assert value, ('Empty translation', key)
        assert collections.Counter(re.findall(r'%L?\d+|%n', key)) == collections.Counter(re.findall(r'%L?\d+|%n', value)), ('Placeholder mismatch', key, value)
        assert re.findall(r'(?:href|src)=[\'\"](.*?)[\'\"]', key) == re.findall(r'(?:href|src)=[\'\"](.*?)[\'\"]', value), ('Link mismatch', key)
        assert collections.Counter(re.findall(r'\{[A-Za-z][\w.]*\}', key)) == collections.Counter(re.findall(r'\{[A-Za-z][\w.]*\}', value)), ('Command variable mismatch', key)
    output = ROOT / 'resources/i18n/ru.json'
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    summary = dict(entries=len(result), source_literals=len(source), unchanged_technical_or_brand_ids=approved_unchanged,
                   changed=sum(key != value for key, value in result.items()))
    (HERE / 'coverage.json').write_text(json.dumps(summary, indent=2), 'utf-8')
    print(json.dumps(summary))
    return result

if __name__ == '__main__':build()
