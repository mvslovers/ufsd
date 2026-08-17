#!/usr/bin/env python3
"""Reject writable file-scope data in the AC(1) load modules.

Why this exists (issue #64)
---------------------------
UFSD, UFSDSSIR and UFSDCLNP are link-edited AC(1).  Fetched from an
APF-authorized library, the job step is authorized before program fetch runs,
and MVS then obtains the job pack area in subpool 252 with **storage key 0**
so that problem-key code cannot patch authorized code.  The STC itself runs
problem state key 8.  Every store into the module's own storage -- i.e. every
write to a C static or a non-const global -- therefore takes a protection
exception:

    PSW AT ENTRY TO ABEND  078D2000 000ADD0A   ILC 4  INTC 0004
    SPQE ... SPID 252  KEY 0   (DQE covers the fetched module)

Without APF the same module is fetched key 8 and the store goes through
unnoticed, which is why this only shows up on hardened systems.  Authorizing
ourselves later via SVC 244 does not change it either way: that sets JSCBAUTH,
it cannot relabel storage that program fetch already allocated.

The same defect turns fatal a second way if a module is ever marked RENT and
placed in the (key 0, page-protected) LPA.  Keeping module storage read-only
is what makes "reentrant" an honest claim.

So: no mutable file-scope data in an AC(1) module.  Put the counter in
UFSD_STC (a main() local, key 8, reachable through anchor->server_stc), on the
heap, or in CSA behind the usual key-0 window.

UFSFMT is exempt: AC(0) means the job step is never authorized, so MVS fetches
it key 8.

Scope and limits
----------------
This checks the C sources this repo owns.  It cannot see libc370 (audited by
hand for #64: per-task state lives in the heap-allocated CRT, the module-
resident statics are no-CRT fallbacks only).  The precise cross-check is to
compile with `cc370 -S` and look for a store through a register loaded from
`=A(@Vn)` -- that is what found the two offenders in the first place.

Usage: tools/check-module-data.py [project.toml]
"""

import glob
import os
import re
import sys

try:
    import tomllib
except ImportError:                                     # Python < 3.11
    sys.exit("check-module-data: needs Python 3.11+ (tomllib)")


def strip_noise(src):
    """Blank out comments, string/char literals and preprocessor lines.

    Newlines are preserved so reported line numbers stay usable.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and src[i:i + 2] == '/*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
            continue
        if c == '/' and src[i:i + 2] == '//':
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
            continue
        if c in '"\'':
            quote, j = c, i + 1
            while j < n and src[j] != quote:
                j += 2 if src[j] == '\\' else 1
            out.append('""' + ' ' * max(0, j - i - 1))
            i = j + 1
            continue
        out.append(c)
        i += 1
    text = ''.join(out)
    return '\n'.join('' if l.lstrip().startswith('#') else l
                     for l in text.split('\n'))


def declarations(src):
    """Yield (line, text, depth) for every statement, file scope and inside
    function bodies alike -- a function-local `static` is module storage too.

    A '{' right after '=' or ',' opens an initializer, not a block.
    """
    depth, init, buf, line, start = 0, 0, '', 1, 1
    for ch in src:
        if ch == '\n':
            line += 1
        if ch == '{':
            if init or buf.rstrip().endswith(('=', ',')):
                init += 1               # aggregate initializer, keep reading
                buf += ' '
                continue
            depth += 1
            buf, start = '', line
            continue
        if ch == '}':
            if init:
                init -= 1
                buf += ' '
                continue
            depth = max(0, depth - 1)
            buf, start = '', line
            continue
        if ch == ';' and not init:
            head = ' '.join(buf.split())
            if head:
                yield start, head, depth
            buf, start = '', line
            continue
        if not buf.strip():
            start = line
        buf += ch


IS_FUNC = re.compile(r'\([^)]*\)\s*$')
IS_FUNC_PTR = re.compile(r'\(\s*\*')
SKIPPABLE = re.compile(r'\b(typedef|extern)\b')
IS_CONST = re.compile(r'\bconst\b')
IS_TAG_ONLY = re.compile(r'^(struct|union|enum)\s+\w+$')


def mutable(head, depth):
    if SKIPPABLE.search(head) or IS_CONST.search(head):
        return False
    if depth and not head.startswith('static'):
        return False        # an ordinary local lives on the stack
    if IS_TAG_ONLY.match(head):
        return False
    if IS_FUNC.search(head) and not IS_FUNC_PTR.search(head):
        return False        # prototype or definition head
    return bool(re.search(r'\w', head))


def sources_of(module, root):
    files = []
    for pattern in module.get('sources', []):
        files += glob.glob(os.path.join(root, pattern))
    dropped = set()
    for pattern in module.get('exclude', []):
        dropped |= set(glob.glob(os.path.join(root, pattern)))
    return sorted(set(files) - dropped)


def main(argv):
    toml = argv[1] if len(argv) > 1 else 'project.toml'
    root = os.path.dirname(os.path.abspath(toml)) or '.'
    with open(toml, 'rb') as fh:
        project = tomllib.load(fh)

    findings = []
    checked = 0
    for module in project.get('module', []):
        if module.get('ac', 0) != 1:
            continue
        for path in sources_of(module, root):
            checked += 1
            src = strip_noise(open(path, encoding='utf-8',
                                   errors='replace').read())
            for line, head, depth in declarations(src):
                if mutable(head, depth):
                    findings.append((module['name'],
                                     os.path.relpath(path, root), line, head))

    if not findings:
        print(f"check-module-data: {checked} sources clean "
              f"(no writable file-scope data in AC(1) modules)")
        return 0

    print("check-module-data: writable file-scope data in an AC(1) module\n")
    for name, path, line, head in findings:
        print(f"  {path}:{line}: {head[:100]}   [{name}]")
    print("\nFetched from an APF-authorized library these modules land in "
          "key-0 storage;\na key-8 store into them abends S0C4 (issue #64). "
          "Move the value into\nUFSD_STC, onto the heap, or into CSA behind "
          "a key-0 window -- or make it const.")
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
