#!/usr/bin/env bash
set -euo pipefail

cmake --preset tests
cmake --build --preset tests
ctest --preset tests

(cd cli && go test ./...)
