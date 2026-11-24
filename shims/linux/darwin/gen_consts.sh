#!/bin/sh
# Regenerate dw_consts.h from the macOS SDK (run on a Mac).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
cc -w "$here/gen_consts.c" -o "$tmp/gen_consts"
"$tmp/gen_consts" > "$here/dw_consts.h"
echo "wrote $here/dw_consts.h"
