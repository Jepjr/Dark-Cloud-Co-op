"""snapregions.py: the snapshot's state set for one build (port/src/coop/snapshot_state.cpp reads it).

  py -3 -I snapregions.py <build>\\darkcloud.map --allow snapshot-allow.txt [--cxxfilt llvm-cxxfilt.exe] [-o <build>\\darkcloud.snapregions]

Reads lld's link map (DC_FIXED_LAYOUT links with -Wl,-Map) and writes one line a global, "ps2|port <offset hex>
<size hex> <name>": every global of the dc_ps2 objects (the decomp's) in the writable output sections, and the port
globals the allow list names. With -fdata-sections each global is one input section, so its size is exact.

The allow list: one pattern a line, fnmatch against the demangled name ("sym:" may be left out), or "obj:<glob>"
against the object's path (every global of that object), and "!" before either to take globals back out. The last rule
that matches a global decides. A demangled name matches as the demangler writes it ("(anonymous namespace)"), with its
spaces as "_" (as the region file writes it), or bare: without "dc::<ns>::" and "(anonymous namespace)::" (repin, Chronicle
9418a550's namespaces). Lines from "#" on are comments.

coop-offthread (2026-11-09, the shared-state review's option 4): a dc_ps2 global is in the set unless a "!" rule takes it out,
written "!ps2:<name>" (a ps2: rule is asked of dc_ps2 globals alone, and other rules never are). Menu state is not world state: the battle menu's working
globals leave the set this way, so a join or a state file no longer gives a copy another copy's menu.

--src DIR (coop-stateload, 2026-10-29): the port globals of the set that are a test switch cached in a function-local static
(a "static ... name = TestEnv...(" line in DIR's .cpp files, matched by function and name against "<function>()::name") are listed and
fail the run: each copy keeps its own test settings, so a state set taken by a run with other settings (a state fixture,
a catch-up join between copies set up differently) must not carry them. Take each back out with "!" in the allow list.

Prints "snapregions: ps2 N globals B bytes, port N globals B bytes, M allow patterns matched nothing" last. Exit 0
when it wrote the file, 2 when not.
"""

import argparse
import fnmatch
import os
import re
import subprocess
import sys

IMAGE_BASE = 0x140000000
WRITABLE = ('.data', '.bss')
LINE = re.compile(r'^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\d+)(\s+)(\S.*)$')
INPUT = re.compile(r'^(.*):\((\.[^)]*)\)$')


def read_map(path):
    """Yields (output section, object path, input section, rva, size, [symbols]) for each input section."""
    out_section = None
    current = None
    depths = []
    with open(path, 'r', errors='replace') as f:
        for raw in f:
            m = LINE.match(raw.rstrip('\r\n'))
            if not m:
                continue
            address, size, _, spaces, rest = int(m.group(1), 16), int(m.group(2), 16), m.group(3), len(m.group(4)), m.group(5)
            if address >= IMAGE_BASE:
                address -= IMAGE_BASE
            if spaces not in depths:
                depths.append(spaces)
                depths.sort()
            depth = depths.index(spaces)
            got = INPUT.match(rest)
            if got:
                if current:
                    yield current
                current = [out_section, got.group(1), got.group(2), address, size, []]
            elif depth == 0 or (rest.startswith('.') and current is None):
                if current:
                    yield current
                    current = None
                out_section = rest.split()[0]
            elif current is not None:
                current[5].append(rest.strip())
    if current:
        yield current


def demangle(names, cxxfilt):
    if not cxxfilt or not names:
        return {n: n for n in names}
    out = subprocess.run([cxxfilt], input='\n'.join(names), capture_output=True, text=True, errors='replace').stdout.splitlines()
    if len(out) != len(names):
        return {n: n for n in names}
    return dict(zip(names, out))


def bare(name):
    """A demangled name without the port's namespaces: "dc::game::(anonymous namespace)::MenuMes" -> "MenuMes" (repin,
    Chronicle 9418a550 put the game in dc::game and many of its globals in anonymous namespaces; a rule names the global
    as the game does, so it keeps matching wherever upstream moves it)."""
    name = re.sub(r'(?:(?<![A-Za-z0-9])dc::\w+::|\(anonymous namespace\)::)', '', name)  # also inside a signature: "_dc::game::CTexture*"
    return re.sub(r' \(\.\d+\)$', '', name)  # a piece of a global the optimizer split ("EdCur (.0)"): the global's rules


def read_allow(path):
    rules = []
    with open(path, 'r') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            negate = line.startswith('!')
            if negate:
                line = line[1:].strip()
            kind, pattern = ('obj', line[4:]) if line.startswith('obj:') else ('ps2', line[4:]) if line.startswith('ps2:') else ('sym', line[4:] if line.startswith('sym:') else line)
            rules.append({'negate': negate, 'kind': kind, 'pattern': pattern, 'hits': 0})
    return rules


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('map')
    parser.add_argument('--allow', required=True)
    parser.add_argument('--cxxfilt', default='')
    parser.add_argument('-o', '--out', default='')
    parser.add_argument('--src', default='')
    args = parser.parse_args()
    out_path = args.out or os.path.splitext(args.map)[0] + '.snapregions'

    sections = [s for s in read_map(args.map) if s[0] and s[0].split('$')[0] in WRITABLE and s[4] > 0]
    if not sections:
        print(f'snapregions: no writable input sections in {args.map} (not an lld map?)')
        return 2
    names = sorted({sym for s in sections for sym in s[5]})
    plain = demangle(names, args.cxxfilt)
    rules = read_allow(args.allow)

    ps2, port = [], []
    for out_section, obj, in_section, rva, size, symbols in sections:
        norm = obj.replace('\\', '/')
        label = plain.get(symbols[0], symbols[0]) if symbols else in_section
        label = re.sub(r'\s+', '_', label)
        def hit(rule):
            if rule['kind'] == 'obj':
                return fnmatch.fnmatch(norm, rule['pattern']) or fnmatch.fnmatch(norm, '*/' + rule['pattern'])
            def forms(s):
                p = plain.get(s, s)
                return (p, re.sub(r'\s+', '_', p), bare(p), re.sub(r'\s+', '_', bare(p)), s)
            return any(fnmatch.fnmatchcase(f, rule['pattern']) for s in symbols for f in forms(s))

        if '/dc_ps2.dir/' in norm:
            # (coop-offthread: in unless the last "!" rule naming it takes it out; only "!" rules are asked)
            wanted = True
            for rule in rules:
                if rule['negate'] and rule['kind'] == 'ps2' and hit(rule):
                    rule['hits'] += 1
                    wanted = False
            if wanted:
                ps2.append((rva, size, label))
            continue
        if '/dc_port.dir/' not in norm:
            continue  # dc_host (Kazarov), the CRT and libraries: never game state
        wanted = False
        for rule in rules:
            if rule['kind'] != 'ps2' and hit(rule):
                rule['hits'] += 1
                wanted = not rule['negate']
        if wanted:
            port.append((rva, size, label))

    with open(out_path, 'w', newline='\n') as f:
        f.write(f'# snapregions.py from {os.path.basename(args.map)}: kind offset size name\n')
        for kind, entries in (('ps2', ps2), ('port', port)):
            for rva, size, label in sorted(entries):
                f.write(f'{kind} {rva:x} {size:x} {label}\n')

    unmatched = [r for r in rules if r['hits'] == 0]
    for rule in unmatched:
        print(f'  allow pattern matched nothing: {"!" if rule["negate"] else ""}{rule["kind"]}:{rule["pattern"]}')
    switches = []
    if args.src:
        cached = set()   # (function, static) pairs: the function is the last "name(...) {" line at column 0 before it
        for root, _, files in os.walk(args.src):
            for name in files:
                if name.endswith('.cpp'):
                    with open(os.path.join(root, name), 'r', errors='replace') as src:
                        fn = ''
                        for line in src:
                            m = re.match(r'^[A-Za-z_][\w:<>,*& ]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?\{', line)
                            if m: fn = m.group(1)
                            # (repin, Chronicle 9418a550: the game's own switches read EnvIs / std::getenv, and sit in dc_ps2)
                            m = re.search(r'static\s+(?:const\s+)?[\w:<>]+\s*\*?\s*(\w+)\s*=\s*(?:TestEnv\w*|EnvIs|std::getenv|getenv)\(', line)
                            if m and fn: cached.add((fn, m.group(1)))
        switches = [label for _, _, label in ps2 + port
                    if (m := re.search(r'(\w+)\([^()]*(?:\([^()]*\)[^()]*)*\)::(\w+)$', label)) and (m.group(1), m.group(2)) in cached]
        for label in switches:
            print(f'  test switch in the state set (take it out with "!" in the allow list): {label}')
    print(f'snapregions: ps2 {len(ps2)} globals {sum(e[1] for e in ps2)} bytes, port {len(port)} globals {sum(e[1] for e in port)} bytes, '
          f'{len(unmatched)} allow patterns matched nothing, {len(switches)} test switches in the set, to {out_path}')
    return 2 if switches else 0


if __name__ == '__main__':
    sys.exit(main())
