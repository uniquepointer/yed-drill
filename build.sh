#!/usr/bin/env bash
gcc -o selene.so selene.c $(yed --print-cflags) $(yed --print-ldflags)
