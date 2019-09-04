#!/bin/sh

echo ":ss:M::\x55\x00\x20\x19:\xFF\xFF\xFF\xFF:$PWD/build/interpreter:PF" > /proc/sys/fs/binfmt_misc/register
