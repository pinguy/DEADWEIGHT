#!/bin/sh
set -eu
cd "$(dirname "$0")"
mode=${1:-native}
case "$mode" in
  native)
    ${CC:-cc} -std=c11 -O2 -Wall -Wextra DEADWEIGHT.c -o deadweight-native -lm -ldl
    printf 'Built ./deadweight-native\n'
    ;;
  packed)
    COPT_EXTRA_LDLIBS='-lm -ldl' COPT_SOURCE_PREPASS=0 OUT="$PWD/deadweight" sh toolkit/build_asm_syscall.sh DEADWEIGHT.c
    ;;
  *) printf 'Usage: ./build.sh [native|packed]\n' >&2; exit 2 ;;
esac
