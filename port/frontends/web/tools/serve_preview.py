#!/usr/bin/env python3
"""Serve the current web bundle without caching during development."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class PreviewHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', required=True)
    parser.add_argument('--port', type=int, default=8091)
    parser.add_argument('--directory', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'dist')
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port),
                                partial(PreviewHandler, directory=str(args.directory.resolve())))
    server.serve_forever()


if __name__ == '__main__':
    main()
