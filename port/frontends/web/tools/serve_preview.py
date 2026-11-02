#!/usr/bin/env python3
"""Serve the current web bundle without caching, with byte ranges for video seeking."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re


class PreviewHandler(SimpleHTTPRequestHandler):
    def send_head(self):
        self.remaining = None
        request = self.headers.get('Range')
        path = Path(self.translate_path(self.path))
        if not request or not path.is_file():
            return super().send_head()
        size = path.stat().st_size
        match = re.fullmatch(r'bytes=(\d*)-(\d*)', request)
        if match and any(match.groups()):
            first, last = match.groups()
            start = int(first) if first else max(0, size-int(last))
            end = min(size-1, int(last)) if first and last else size-1
        else:
            start, end = size, -1
        if not 0 <= start <= end < size:
            self.send_response(416)
            self.send_header('Content-Range', f'bytes */{size}')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return None
        stream = path.open('rb')
        stream.seek(start)
        self.remaining = end-start+1
        self.send_response(206)
        self.send_header('Content-Type', self.guess_type(str(path)))
        self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Content-Length', str(self.remaining))
        self.send_header('Last-Modified', self.date_time_string(path.stat().st_mtime))
        self.end_headers()
        return stream

    def copyfile(self, source, outputfile):
        if self.remaining is None:
            return super().copyfile(source, outputfile)
        while self.remaining:
            chunk = source.read(min(self.remaining, 64*1024))
            if not chunk:
                break
            outputfile.write(chunk)
            self.remaining -= len(chunk)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Accept-Ranges', 'bytes')
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
