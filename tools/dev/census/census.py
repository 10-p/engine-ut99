#!/usr/bin/env python3
"""census.py <build-dir> <source-root> [<extra-source-root> ...] -- which engine functions ever ran.

Runs gcov (JSON) on every instrumented object under <build-dir> (a --coverage build after the test runs),
then aggregates per function across translation units: a function counts as executed if its own entry count
is non-zero in any TU or any line inside it ran in any TU (so inlined bodies count). Only functions whose
source lies under one of the roots are reported. Writes census.json (every function, `ran` true/false) into the
current directory and prints a per-directory summary. ufront 2.83; the recipe is in BUILD.md "Coverage census".
"""
import json, os, subprocess, sys, collections, tempfile

build, roots = sys.argv[1], [os.path.realpath(r) for r in sys.argv[2:]]
gcnos = []
for d, _, fs in os.walk(build):
    gcnos += [os.path.join(d, f) for f in fs if f.endswith('.gcno')]

funcs = {}                                   # (file, start_line, name) -> dict
lines = collections.defaultdict(int)         # (file, line) -> max count
with tempfile.TemporaryDirectory() as tmp:
    for g in gcnos:
        r = subprocess.run(['gcov', '--json-format', '--stdout', '-o', os.path.dirname(g), g],
                           cwd=tmp, capture_output=True)
        for chunk in r.stdout.split(b'\n'):
            if not chunk.strip():
                continue
            try:
                doc = json.loads(chunk)
            except ValueError:
                continue
            cwd = doc.get('current_working_directory', '')
            for f in doc.get('files', []):
                path = os.path.realpath(os.path.join(cwd, f['file']))
                if not any(path.startswith(r + os.sep) for r in roots):
                    continue
                for ln in f.get('lines', []):
                    key = (path, ln['line_number'])
                    lines[key] = max(lines[key], ln['count'])
                for fn in f.get('functions', []):
                    key = (path, fn['start_line'], fn['demangled_name'])
                    e = funcs.setdefault(key, {'file': path, 'start': fn['start_line'], 'end': fn['end_line'],
                                               'name': fn['demangled_name'], 'count': 0})
                    e['count'] = max(e['count'], fn['execution_count'])
                    e['end'] = max(e['end'], fn['end_line'])

for e in funcs.values():
    ran = e['count'] > 0 or any(lines.get((e['file'], n), 0) > 0 for n in range(e['start'], e['end'] + 1))
    e['ran'] = ran

def area(p):
    for r in roots:
        if p.startswith(r + os.sep):
            rel = os.path.relpath(p, r).split(os.sep)
            return os.path.basename(r) + '/' + rel[0]
    return '?'

by = collections.defaultdict(lambda: [0, 0])
for e in funcs.values():
    by[area(e['file'])][0] += 1
    by[area(e['file'])][1] += e['ran']
print("%-34s %7s %7s %7s" % ('area', 'funcs', 'ran', 'never'))
T = [0, 0]
for a in sorted(by):
    n, r = by[a]; T[0] += n; T[1] += r
    print("%-34s %7d %7d %7d" % (a, n, r, n - r))
print("%-34s %7d %7d %7d" % ('TOTAL', T[0], T[1], T[0] - T[1]))
json.dump(sorted(funcs.values(), key=lambda e: (e['file'], e['start'])), open('census.json', 'w'), indent=0)
