#!/bin/sh
# smoke.sh BINARY: the tools parse, and the runtime starts and fails cleanly without game data.
set -e
for f in tools/*.py translation/gen/*.py; do python3 -c "import ast,sys; ast.parse(open(sys.argv[1], encoding='utf-8').read())" "$f"; done
set +e
PERSONA_HEADLESS=1 PERSONA_DATA=no-such-dir "$1" > smoke.log 2>&1
code=$?
set -e
cat smoke.log
[ "$code" -ne 0 ] || { echo "expected a non-zero exit without game data"; exit 1; }
grep -q "game data folder 'no-such-dir' not found" smoke.log
echo "smoke test passed"
