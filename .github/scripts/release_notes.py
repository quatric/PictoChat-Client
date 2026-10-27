#!/usr/bin/env python3
"""Release notes from CHANGELOG.md.

    release_notes.py 1.3.1          notes for the v1.3.1 release
    release_notes.py latest <sha>   notes for the rolling "latest" release

A release gets its own changelog entry, how to install it, and the entries of
every earlier version folded away under "Changelog".
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def parse_version(v):
    return tuple(int(x) for x in v.split("."))


def entries():
    """[(version, date, body)] newest first, as CHANGELOG.md lists them."""
    text = open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8").read()
    parts = re.split(r"^## (\d+\.\d+\.\d+) \(([^)]*)\)\s*$", text, flags=re.M)
    return [(parts[i], parts[i + 1], parts[i + 2].strip()) for i in range(1, len(parts), 3)]


def installing(version):
    """How to install, for the consoles this version shipped on."""
    v = parse_version(version) if version else None
    lines = ["- **Wii**: extract PictoChat-Wii.zip to the root of your SD card or USB drive and open PictoChat from the Homebrew Channel."]
    if v is None or v >= (1, 2, 1):
        lines.append("- **Wii U**: extract PictoChat-WUP.zip to the root of your SD card and open PictoChat from the Wii U Menu (requires Aroma).")
        lines.append("- **3DS**: extract PictoChat-3DS.zip to the root of your SD card and open PictoChat from the Homebrew Launcher, or install PictoChat-3DS.cia with FBI.")
    if v is None or v >= (1, 4, 0):
        lines.append("- **Switch**: extract PictoChat-Switch.zip to the root of your SD card and open PictoChat from the Homebrew Menu.")
    # Wii builds update themselves from 1.2.0 on, Wii U and 3DS ones from 1.3.0
    if v is None or v > (1, 3, 0):
        lines.append("\nAlready have PictoChat? Open it and press **A** on the update popup (Wii 1.2.0 or later, Wii U and 3DS 1.3.0 or later). Your saved files are kept.")
    elif v > (1, 2, 0):
        lines.append("\nAlready have PictoChat 1.2.0 or later on the Wii? Open it and press **A** on the update popup. Your saved files are kept.")
    return "## Installing\n" + "\n".join(lines)


def folded(items, title):
    if not items:
        return ""
    body = "\n\n".join("### %s (%s)\n\n%s" % (v, d, b) for v, d, b in items)
    return "<details>\n<summary>%s</summary>\n\n%s\n\n</details>" % (title, body)


def notes(version, sha=None):
    all_entries = entries()
    if version == "latest":
        head = "Latest build of PictoChat from %s." % sha if sha else "Latest build of PictoChat."
        return "\n\n".join([head, installing(None), folded(all_entries, "Changelog")]) + "\n"
    mine = [e for e in all_entries if e[0] == version]
    older = [e for e in all_entries if parse_version(e[0]) < parse_version(version)]
    if mine:
        new = "## What's new\n\n" + mine[0][2]
    else:
        sys.stderr.write("release_notes: no CHANGELOG.md entry for %s\n" % version)
        new = "See CHANGELOG.md for what's new."
    return "\n\n".join(p for p in [new, installing(version), folded(older, "Changelog")] if p) + "\n"


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.stdout.write(notes(sys.argv[1].lstrip("v"), sys.argv[2] if len(sys.argv) > 2 else None))
