#!/bin/sh

set -e

make
./build/compiler $1 /tmp/a.ssa
./build/assembler /tmp/a.ssa /tmp/a.sso
./build/linker /tmp/a.sso /tmp/_start.sso $2
