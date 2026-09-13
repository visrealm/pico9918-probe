"""Export tracked edits and new files without changing either repository's index."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
destination = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root.parent / "debugprobe-pico9918.patch"

def git(*args, allowed=(0,)):
    result = subprocess.run(["git", "-c", "core.autocrlf=true", *args], cwd=root, capture_output=True)
    if result.returncode not in allowed:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return result.stdout

patch = git("diff", "--binary", "HEAD")
for name in git("ls-files", "--others", "--exclude-standard", "-z").split(b"\0"):
    if name:
        patch += git("diff", "--binary", "--no-index", "--", "/dev/null", name.decode(), allowed=(0, 1))
patch = patch.replace(b"\r\n", b"\n")
destination.write_bytes(patch)
print(f"Exported {len(patch)} bytes to {destination}")
