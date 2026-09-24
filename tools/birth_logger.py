"""
Developer tool: record every in-game night to the birth log (see
mcp_server/birth_log.py) to check breeding hypotheses against real births.
Polls the game every 10 s; stop with Ctrl+C.

    python tools\birth_logger.py            # log (optionally: poll seconds)
    python tools\birth_logger.py --report   # analyse what's logged

The MCP server can log too if MEWGENICS_BIRTH_LOG=on is set; running both is
harmless (a night is only written once).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mcp_server"))

import birth_log  # noqa: E402
import server  # noqa: E402


def main():
    if "--report" in sys.argv:
        import json
        report = birth_log.analyze(birth_log.read_log(server.BIRTH_LOGGER.path))
        print(json.dumps(report, ensure_ascii=False, indent=1))
        return
    logger = server.BIRTH_LOGGER
    print(f"birth log: {logger.path}")

    def on_record(rec):
        names = ", ".join((b.get("kitten_cat") or {}).get("name") or str(b["kitten"]) for b in rec["births"])
        print(f"day {rec['day_after']}: {len(rec['births'])} kitten(s){': ' + names if names else ''}"
              + (" (gap: missed a day)" if rec.get("gap") else ""))

    logger.on_event = lambda e: print(f"  [{e.get('day', '')}] {e['what']}", flush=True)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    interval = float(args[0]) if args else birth_log.POLL_SECONDS
    try:
        birth_log.run_poller(server.birth_log_snapshot, logger, interval=interval, on_record=on_record)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
