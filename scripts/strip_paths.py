#!/usr/bin/env python3
# Overwrites the build machine's folders in a packaged binary with slashes: C++ __FILE__ strings (assertions) keep their
# paths otherwise, and an installed UE can't take a compiler flag for it. Same length, so the file's layout doesn't change.
# usage: scripts/strip_paths.py <binary> <folder>...
import pathlib, sys

binary = pathlib.Path(sys.argv[1])
data = binary.read_bytes()
for folder in sys.argv[2:]:
    prefix = folder.rstrip("/").encode() + b"/"
    print(f"{binary.name}: {data.count(prefix)} paths under {folder}")
    data = data.replace(prefix, b"/" * len(prefix))
binary.write_bytes(data)
