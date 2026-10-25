"""`orbi run` starts the daemon."""
import argparse
import asyncio

from . import server
from .daemon import Daemon


def main():
    parser = argparse.ArgumentParser(prog="orbi")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("run", help="start the daemon")
    parser.parse_args()

    daemon = Daemon()
    try:
        asyncio.run(server.run(daemon))
    except KeyboardInterrupt:
        pass
