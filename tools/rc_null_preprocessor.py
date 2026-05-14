"""No-op preprocessor for windres.

windres always wants a C preprocessor, even when the .rc has no #include
or #define. On Windows + MinGW, that preprocessor is cc1.exe, which mangles
include paths containing spaces. This script just prints the input file
unchanged so windres skips real preprocessing entirely.

Invocation by windres:
    python rc_null_preprocessor.py <flags...> <input.rc>
"""
import sys

with open(sys.argv[-1], "r", encoding="utf-8") as f:
    sys.stdout.write(f.read())
