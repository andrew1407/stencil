# Dev server for the browser app: python's stdlib http.server plus Cache-Control:
# no-store, so a plain browser refresh always picks up edited JS/CSS (the bare
# `python3 -m http.server` lets Chrome heuristically cache module files, which
# made stale UIs look like unfixed bugs during development). Stdlib only.
import http.server
import os
import sys


class NoStoreHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


if __name__ == '__main__':
    addr = os.environ.get('ADDR', 'localhost')
    port = int(os.environ.get('PORT', '8080'))
    with http.server.ThreadingHTTPServer((addr, port), NoStoreHandler) as srv:
        print(f'Serving on http://{addr}:{port} (Cache-Control: no-store)')
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            sys.exit(0)
