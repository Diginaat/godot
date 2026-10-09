"""Lists class reference entries this fork added that have no description.

Every class, property, method, constant, signal and project setting of the
fork needs one: the editor's help and the inspector tooltips come from these
XML files. Entries that upstream Godot already has are skipped (they are
upstream's to write), as are inherited property overrides.

  python devtools/docs/check_class_docs.py        # counts per file
  python devtools/docs/check_class_docs.py -v     # every missing entry

Needs the upstream remote (git fetch upstream master). Exit code 1 if
anything is missing.
"""
import glob
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
os.chdir(ROOT)
BASE = subprocess.check_output(["git", "merge-base", "HEAD", "upstream/master"], text=True).strip()
FILES = sorted(set(p.replace("\\", "/") for p in glob.glob("doc/classes/*.xml") + glob.glob("modules/*/doc_classes/*.xml")))


def entries(root):
    out = {
        ("class", "brief"): (root.findtext("brief_description") or "").strip(),
        ("class", "description"): (root.findtext("description") or "").strip(),
    }
    for tag in ["method", "member", "constant", "signal", "theme_item", "annotation", "operator", "constructor"]:
        for e in root.iter(tag):
            if tag == "member" and e.get("overrides"):
                continue
            key = (tag, e.get("name") + ("|" + e.get("data_type", "") if tag == "theme_item" else ""))
            direct = tag in ("member", "constant", "theme_item")
            out[key] = ((e.text or "") if direct else (e.findtext("description") or "")).strip()
    return out


missing_total = 0
for f in FILES:
    try:
        upstream = entries(ET.fromstring(subprocess.check_output(["git", "show", f"{BASE}:{f}"], stderr=subprocess.DEVNULL, encoding="utf-8")))
    except subprocess.CalledProcessError:
        upstream = {}
    missing = [k for k, v in entries(ET.parse(f).getroot()).items() if not v and k not in upstream]
    if missing:
        missing_total += len(missing)
        print(f"{f}: {len(missing)}")
        if "-v" in sys.argv:
            for tag, name in missing:
                print(f"    {tag} {name}")
print(f"{missing_total} fork entries without a description")
sys.exit(1 if missing_total else 0)
