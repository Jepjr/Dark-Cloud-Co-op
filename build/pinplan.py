# pinplan.py (coop-cand57, Jep): the arenas' pin plan, from the link map, so no two arena owners share a slot.
#
# Each arena owner's block is mapped at kPinBase + slot * 256 MiB, the slot a splitmix64 hash of the owner's offset in the
# image (Kazarov arena.cpp, recipe K006). Two owners whose offsets hash to one slot left the second unpinned: train 56's
# MapModelBuffer and MasekiModelBuffer, 96 floor rows crashed on a fixture's stale pointers. Probe k is another fixed slot
# (the offset with k in its high bits). This plan gives every owner its probe from the map alone, so the answer does not
# hang on which owner carves first: every symbol start in a data section is taken as a possible owner (an extra entry for a
# non-owner costs nothing; an owner inside a struct is not a symbol start and probes at run time instead, which the state
# set's base check covers). In offset order, each takes the lowest probe whose slot no earlier one took and that is no
# other one's probe 0. The plan lists the owners whose probe is not 0: "<offset hex> <probe>  # <symbol>".
#
#   py -3 pinplan.py <darkcloud.map> -o <darkcloud.pinplan> [--check]
# --check: write nothing; compare with the -o file and fail if it differs (the gate: the bin's plan is its map's).
# Exit 1, "pinplan: FAIL ...", when an owner finds no free slot in its probes. PINPLAN_MUTATE=no_probe (the self-test's twin):
# probe 0 only, so any two owners sharing a slot fail, as the old fallback left them.
import argparse, os, re, sys

M = (1 << 64) - 1
SLOTS = (64 << 40) >> 28
PROBES = 8


def slot(offset, probe=0):
    x = ((offset ^ (probe << 52)) + 0x9e3779b97f4a7c15) & M
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & M
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & M
    x ^= x >> 31
    return x % SLOTS


def owners(map_path):
    out_re = re.compile(r'^([0-9a-f]{8,16}) ([0-9a-f]{8,16}) +\d+ (\.\S+)\s*$')
    line_re = re.compile(r'^([0-9a-f]{8,16}) ([0-9a-f]{8,16}) +\d+\s+(\S.*)$')
    data = []  # (start, end) of the output sections that are not code
    found = {}
    with open(map_path, encoding='latin-1') as f:
        for line in f:
            m = out_re.match(line)
            if m:
                name = m.group(3)
                if name not in ('.text', '.pdata', '.xdata') and not name.startswith('.debug'):
                    start = int(m.group(1), 16)
                    data.append((start, start + int(m.group(2), 16)))
                continue
            m = line_re.match(line)
            if not m:
                continue
            at = int(m.group(1), 16)
            if at not in found and any(s <= at < e for s, e in data):
                found[at] = m.group(3).strip()
    return found


def plan(found, probes):
    zero = {}
    for at in found:
        zero.setdefault(slot(at), []).append(at)
    taken, out, failed = set(), {}, []
    for at in sorted(found):
        for k in range(probes):
            s = slot(at, k)
            if s in taken or (k > 0 and s in zero):
                continue
            taken.add(s)
            if k:
                out[at] = k
            break
        else:
            others = [found[o] for o in zero.get(slot(at), []) if o != at]
            failed.append(f'{found[at]} (image+0x{at:x}) shares slot {slot(at)} with {", ".join(others) or "a probe of another"}')
    return out, failed, sum(1 for v in zero.values() if len(v) > 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map')
    ap.add_argument('-o', required=True)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    probes = 1 if os.environ.get('PINPLAN_MUTATE') == 'no_probe' else PROBES
    found = owners(a.map)
    out, failed, shared = plan(found, probes)
    text = '# pinplan.py: arena owners whose pin slot is not their first (offset hex, probe)\n' + ''.join(
        f'{at:x} {k}  # {found[at]}\n' for at, k in sorted(out.items()))
    print(f'pinplan: {len(found)} possible owners, {shared} first slots shared, {len(out)} owners planned at another probe')
    if failed:
        for f in failed[:20]:
            print(f'pinplan: FAIL {f}')
        return 1
    if a.check:
        have = open(a.o, encoding='latin-1').read() if os.path.exists(a.o) else ''
        if have != text:
            print(f'pinplan: FAIL {a.o} is not this map\'s plan (built before pinplan, or from another map)')
            return 1
        print('pinplan: the plan beside the exe is its map\'s')
        return 0
    with open(a.o, 'w', encoding='latin-1', newline='\n') as f:
        f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
