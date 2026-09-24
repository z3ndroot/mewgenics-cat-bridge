"""
Run the birth log on its own (without Claude Desktop): polls the game every
20 s and appends a record to the birth log each in-game night.

    python tools\\birth_logger.py

The MCP server does the same in the background while it runs; running both
is harmless (a night is only written once). Stop with Ctrl+C.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mcp_server"))

import birth_log  # noqa: E402
import server  # noqa: E402


def main():
    logger = server.BIRTH_LOGGER
    print(f"birth log: {logger.path}")

    def on_record(rec):
        names = ", ".join((b.get("kitten_cat") or {}).get("name") or str(b["kitten"]) for b in rec["births"])
        print(f"day {rec['day_after']}: {len(rec['births'])} kitten(s){': ' + names if names else ''}"
              + (" (gap: missed a day)" if rec.get("gap") else ""))

    logger.on_event = lambda e: print(f"  [{e.get('day', '')}] {e['what']}", flush=True)
    interval = float(sys.argv[1]) if len(sys.argv) > 1 else birth_log.POLL_SECONDS
    try:
        birth_log.run_poller(server.birth_log_snapshot, logger, interval=interval, on_record=on_record)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
