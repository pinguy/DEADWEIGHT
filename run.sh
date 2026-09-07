#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -x ./deadweight-native ]; then exec ./deadweight-native "$@"; fi
exec ./deadweight "$@"
