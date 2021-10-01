#!/usr/bin/env bash
gcc -o drill.so drill.c $(yed --print-cflags) $(yed --print-ldflags)
