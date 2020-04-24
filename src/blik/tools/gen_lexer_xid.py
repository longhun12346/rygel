#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
import re
import argparse

def parse_properties_xid(f):
    id_start = []
    id_continue = []

    version = next(f).strip("\n\r \t#") + ' -- ' + next(f).strip("\n\r \t#")

    for line in f:
        line = line.strip()

        if not line or line[0] == '#':
            continue

        parts = [part.strip() for part in re.split('[;#]', line)]
        if len(parts) < 3:
            continue

        if '..' in parts[0]:
            start, end = parts[0].split('..')

            if parts[1] == 'ID_Start':
                id_start.append((start, end))
            elif parts[1] == 'ID_Continue':
                id_continue.append((start, end))
        else:
            if parts[1] == 'ID_Start':
                id_start.append((parts[0], parts[0]))
            elif parts[1] == 'ID_Continue':
                id_continue.append((parts[0], parts[0]))

    return (version, id_start, id_continue)

def write_xid_header(version, id_start, id_continue, f):
    f.write(f"""// This Source Code Form is subject to the terms of the Mozilla Public', file = f, sep='')
// License, v. 2.0. If a copy of the MPL was not distributed with this', file = f, sep='')
// file, You can obtain one at http://mozilla.org/MPL/2.0/', file = f, sep='')

// This file is autogenerated by gen_lexer_xid.py
// Version: {version}

namespace RG {{

static const int32_t UnicodeIdStartTable[] = {{""")
    for i, v in enumerate(id_start):
        if i % 5 == 0: f.write('\n   ')
        f.write(f' 0x{v[0]:0>5}, 0x{v[1]:0>5}{"," if i + 1 < len(id_start) else ""}')
    f.write("""
};

static const int32_t UnicodeIdContinueTable[] = {""")
    for i, v in enumerate(id_continue):
        if i % 5 == 0: f.write('\n   ')
        f.write(f' 0x{v[0]:0>5}, 0x{v[1]:0>5}{"," if i + 1 < len(id_continue) else ""}')
    f.write("""
};

}
""")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description = 'Create lexer_xid.hh (blik) from Unicode file DerivedCoreProperties.txt')
    parser.add_argument('filename', metavar = 'source', type = str, nargs = 1,
                        help = 'path to DerivedCoreProperties.txt')
    args = parser.parse_args()

    with open(args.filename[0], 'r') as f:
        version, id_start, id_continue = parse_properties_xid(f)

    write_xid_header(version, id_start, id_continue, sys.stdout)
