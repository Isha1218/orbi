"""`orbi run` starts the daemon."""
import argparse
import asyncio
import threading

from . import server
from .daemon import Daemon
from .panel import Panel


def run_daemon(daemon, panel, harness):
    try:
        asyncio.run(server.run(daemon, harness))
    finally:
        panel.close()  # if the daemon stops, quit the panel too


def main():
    parser = argparse.ArgumentParser(prog="orbi")
    sub = parser.add_subparsers(dest="cmd", required=True)
    run_p = sub.add_parser("run", help="start the daemon")
    run_p.add_argument("--harness", choices=["claude", "hermes"], default="claude", help="agent that does the work")
    args = parser.parse_args()

    panel = Panel()
    daemon = Daemon(panel=panel)
    # The panel's window needs the main thread, so the daemon runs beside it
    thread = threading.Thread(target=run_daemon, args=(daemon, panel, args.harness), daemon=True)
    thread.start()
    try:
        panel.run()
    except KeyboardInterrupt:
        pass
