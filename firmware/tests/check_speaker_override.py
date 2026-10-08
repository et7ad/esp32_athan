#!/usr/bin/env python3
"""Check that firmware/components/speaker is ESPHome's own speaker component plus exactly the ATHAN PATCH blocks.

The override replaces ESPHome's component (components/speaker/README.md). Any difference other than the stored
patch (components/speaker/athan.patch), such as a stray edit or an ESPHome upgrade without re-copying, fails.

    check_speaker_override.py <esphome package dir>            # compare
    check_speaker_override.py <esphome package dir> --update   # rewrite athan.patch after re-applying the patch

syntax_check.sh runs it with the ESPHome it uses.
"""
import difflib
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
OURS = HERE.parent / "components" / "speaker"
PATCH = OURS / "athan.patch"
OWN_FILES = {"README.md", "athan.patch"}


def unified_diff(upstream: pathlib.Path) -> str:
    out = []
    names = sorted(
        {p.relative_to(upstream).as_posix() for p in upstream.rglob("*") if p.is_file() and "__pycache__" not in p.parts}
        | {p.relative_to(OURS).as_posix() for p in OURS.rglob("*") if p.is_file() and "__pycache__" not in p.parts}
    )
    for rel in names:
        if rel in OWN_FILES:
            continue
        a, b = upstream / rel, OURS / rel
        a_lines = a.read_text().splitlines(keepends=True) if a.exists() else []
        b_lines = b.read_text().splitlines(keepends=True) if b.exists() else []
        out.extend(difflib.unified_diff(a_lines, b_lines, f"a/{rel}", f"b/{rel}"))
    return "".join(out)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    upstream = pathlib.Path(sys.argv[1]) / "components" / "speaker"
    if not upstream.is_dir():
        print(f"no speaker component in {sys.argv[1]}")
        return 2
    diff = unified_diff(upstream)
    if "--update" in sys.argv:
        PATCH.write_text(diff)
        print(f"wrote {PATCH} ({diff.count(chr(10))} lines)")
        return 0
    if not PATCH.exists():
        print("athan.patch missing: run with --update")
        return 1
    if diff != PATCH.read_text():
        print("components/speaker differs from ESPHome's speaker + athan.patch:")
        sys.stdout.writelines(difflib.unified_diff(PATCH.read_text().splitlines(keepends=True),
                                                   diff.splitlines(keepends=True), "athan.patch", "actual"))
        return 1
    hunks = diff.count("\n@@ ")
    print(f"   speaker override = ESPHome's speaker + athan.patch ({hunks} hunks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
