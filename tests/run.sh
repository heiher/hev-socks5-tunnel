#!/bin/sh
# Entry point of the hev-socks5-tunnel test suite.
#
#   tests/run.sh func            functional tests (fast, pass/fail)
#   tests/run.sh bench           benchmarks (report numbers only)
#   tests/run.sh all             both, as two separate sessions
#   tests/run.sh list            list tests
#
# Options: -b BIN  -k NAME (repeatable)  -d SECONDS  -x  -v
# See tests/README.md.

set -e
HERE=$(cd "$(dirname "$0")" && pwd)

# The tun device and the network namespaces need root.
if [ "$(id -u)" != 0 ]; then
    exec sudo env "PATH=$PATH" ${PYTHON:+"PYTHON=$PYTHON"} \
        ${CC:+"CC=$CC"} ${BENCH_DURATION:+"BENCH_DURATION=$BENCH_DURATION"} \
        ${GITHUB_STEP_SUMMARY:+"GITHUB_STEP_SUMMARY=$GITHUB_STEP_SUMMARY"} \
        "$0" "$@"
fi

PY=${PYTHON:-python3}
if ! "$PY" -c 'import os,sys; sys.exit(0 if sys.version_info>=(3,12) and hasattr(os,"setns") else 1)' 2>/dev/null; then
    echo "run.sh: Python >= 3.12 is required (set PYTHON=...)" >&2
    exit 2
fi

cd "$HERE/.."
PYTHONPATH="$HERE${PYTHONPATH:+:$PYTHONPATH}" exec "$PY" -m hevtest.main "$@"
