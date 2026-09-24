"""
Build the release zip: everything a player needs to run the bridge, no
compiler required.

    powershell -ExecutionPolicy Bypass -File mod\\build.ps1
    python tools\\package_release.py        # -> dist\\mewgenics-cat-bridge-v<version>.zip

The version comes from MOD_VERSION in mod/cat_bridge/amoeboid.hpp.
"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = "mewgenics-cat-bridge"

FILES = [
    ("bin/cat_bridge.dll", "bin/cat_bridge.dll"),
    ("inject.bat", "inject.bat"),
    ("eject.bat", "eject.bat"),
    ("requirements.txt", "requirements.txt"),
    ("README.md", "README.md"),
    ("README.ru.md", "README.ru.md"),
    ("LICENSE", "LICENSE"),
    ("mod/LICENSE.md", "licenses/mewgenics_randomize_item_picks-LICENSE.md"),
    ("mod/ATTRIBUTION.md", "licenses/ATTRIBUTION.md"),
    ("tools/cat_bridge_dev.py", "tools/cat_bridge_dev.py"),
    ("tools/gpak.py", "tools/gpak.py"),
    ("tools/birth_logger.py", "tools/birth_logger.py"),
]


def main():
    header = (ROOT / "mod/cat_bridge/amoeboid.hpp").read_text(encoding="utf-8")
    version = re.search(r'MOD_VERSION\[\] = "([^"]+)"', header).group(1)
    missing = [src for src, _ in FILES if not (ROOT / src).exists()]
    if missing:
        sys.exit(f"missing: {missing} (build the DLL first: mod\\build.ps1)")

    out = ROOT / "dist" / f"{NAME}-v{version}.zip"
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, dst in FILES:
            z.write(ROOT / src, f"{NAME}/{dst}")
        for py in sorted((ROOT / "mcp_server").glob("*.py")) + [ROOT / "mcp_server/requirements.txt"]:
            z.write(py, f"{NAME}/mcp_server/{py.name}")
    print(out)


if __name__ == "__main__":
    main()
