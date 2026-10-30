#!/usr/bin/env python3
"""Minimal HTTP/HTTPS forward proxy for the tablet, bound to the USB-link address only.

Supports CONNECT (HTTPS tunnels) and absolute-URI requests (plain HTTP). No auth: it only
listens on 172.16.42.2, which exists only on the point-to-point USB link to the tablet.
"""
import select
import socket
import socketserver
import sys

LISTEN = ("172.16.42.2", 3128)


def pipe(a, b):
    socks = [a, b]
    while True:
        r, _, x = select.select(socks, [], socks, 300)
        if x or not r:
            return
        for s in r:
            data = s.recv(65536)
            if not data:
                return
            (b if s is a else a).sendall(data)


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        c = self.request
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = c.recv(65536)
            if not chunk:
                return
            head += chunk
        line, _, rest = head.partition(b"\r\n")
        try:
            method, target, version = line.decode("latin1").split(" ", 2)
        except ValueError:
            return
        try:
            if method == "CONNECT":
                host, port = target.rsplit(":", 1)
                up = socket.create_connection((host, int(port)), timeout=30)
                c.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
                pipe(c, up)
            else:
                if not target.startswith("http://"):
                    c.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
                    return
                hostport, _, path = target[7:].partition("/")
                host, _, port = hostport.partition(":")
                up = socket.create_connection((host, int(port or 80)), timeout=30)
                # Rewrite to origin form and force connection close so one request maps to one socket.
                headers = [h for h in rest.split(b"\r\n") if not h.lower().startswith((b"proxy-connection", b"connection"))]
                up.sendall(f"{method} /{path} {version}\r\n".encode("latin1") + b"\r\n".join(headers[:-2])
                           + b"\r\nConnection: close\r\n\r\n" + b"\r\n".join(headers[-1:]))
                pipe(c, up)
        except OSError as e:
            try:
                c.sendall(f"HTTP/1.1 502 Bad Gateway\r\n\r\n{e}\n".encode())
            except OSError:
                pass


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    with Server(LISTEN, Handler) as s:
        print(f"proxy on {LISTEN[0]}:{LISTEN[1]}", file=sys.stderr, flush=True)
        s.serve_forever()
