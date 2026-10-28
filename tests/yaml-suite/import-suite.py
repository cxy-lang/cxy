#!/usr/bin/env python3
"""Refreshes tests/yaml-suite/cases from a checkout of the yaml-test-suite `data` branch.

  git clone --depth 1 -b data https://github.com/yaml/yaml-test-suite /tmp/yts
  python3 tests/yaml-suite/import-suite.py /tmp/yts

Keeps the cases that have in.yaml and either an `error` marker or a single JSON
document in in.json (multi-document and sub-test cases are skipped).
"""
import json
import os
import shutil
import sys

src = sys.argv[1]
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cases")
shutil.rmtree(dst, ignore_errors=True)
os.makedirs(dst)

count = 0
for case in sorted(os.listdir(src)):
    base = os.path.join(src, case)
    if case.startswith(".") or not os.path.isdir(base):
        continue
    if not os.path.exists(os.path.join(base, "in.yaml")):
        continue

    is_error = os.path.exists(os.path.join(base, "error"))
    if not is_error:
        path = os.path.join(base, "in.json")
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8").read().lstrip()
        try:
            _, end = json.JSONDecoder().raw_decode(text)
        except ValueError:
            continue
        if text[end:].strip():
            continue

    out = os.path.join(dst, case)
    os.makedirs(out)
    shutil.copy(os.path.join(base, "in.yaml"), os.path.join(out, "in.yaml"))
    if is_error:
        open(os.path.join(out, "error"), "w").close()
    else:
        shutil.copy(os.path.join(base, "in.json"), os.path.join(out, "in.json"))
    name = os.path.join(base, "===")
    if os.path.exists(name):
        shutil.copy(name, os.path.join(out, "name"))
    count += 1

print(f"imported {count} cases")
