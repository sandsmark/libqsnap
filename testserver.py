#!/usr/bin/python

from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        print("GET path: " + self.path)

    def do_POST(self):
        print("POST")
        print("path: " + self.path)
        print("headers:")
        for header in self.headers._headers:
            print(header)
        print("body: " + str(self.rfile.read(int(self.headers.get('content-length')))))

serber = HTTPServer(('127.0.0.1', 1234), Handler)
serber.serve_forever(0.5)
