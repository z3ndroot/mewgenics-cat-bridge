"""
Read Mewgenics' resources.gpak archive.

    python gpak.py list [substring]            # list entries (optionally filtered)
    python gpak.py extract <out_dir> <substr>  # extract entries whose path contains substr

Archive path: $MEWGENICS_GPAK, or the default in mcp_server/game_data.py.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mcp_server"))
from game_data import GPAK_PATH, read_gpak_index  # noqa: E402


def main():
    args = sys.argv[1:]
    with open(GPAK_PATH, "rb") as f:
        index = read_gpak_index(f)
        if args[:1] == ["list"]:
            needle = args[1] if len(args) > 1 else ""
            for name, _, size in index:
                if needle in name:
                    print(f"{size:>10}  {name}")
        elif args[:1] == ["extract"] and len(args) == 3:
            out_dir, needle = Path(args[1]), args[2]
            for name, offset, size in index:
                if needle in name:
                    dest = out_dir / name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    f.seek(offset)
                    dest.write_bytes(f.read(size))
                    print(dest)
        else:
            print(__doc__)
            sys.exit(2)


if __name__ == "__main__":
    main()
