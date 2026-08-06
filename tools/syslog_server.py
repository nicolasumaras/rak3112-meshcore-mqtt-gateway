#!/usr/bin/env python3
"""Minimal syslog collector for the RAK3112 gateway.

Listens for RFC 3164 UDP syslog and appends to a log file. Heartbeat lines are
also written to a CSV so a multi-day soak can be plotted without post-processing.

    python3 syslog_server.py                      # port 514 needs root
    sudo python3 syslog_server.py
    python3 syslog_server.py --port 5514          # unprivileged

Point the gateway at this host with serial menu option 16.

No dependencies beyond the standard library, so it runs anywhere without a venv.
"""
import argparse
import csv
import os
import re
import socket
import sys
from datetime import datetime, timezone

SEVERITY = {0: "emerg", 1: "alert", 2: "crit", 3: "err",
            4: "warn", 5: "notice", 6: "info", 7: "debug"}

PRI_RE = re.compile(r"^<(\d+)>(.*)$", re.S)
# hb uptime=123 heap=45678 minheap=... maxblock=... rssi=-52 rx=1 tx=2 fwd=3 fail=0 contacts=1
HB_RE = re.compile(r"\bhb\b\s+(.*)$")
KV_RE = re.compile(r"(\w+)=(-?[\d.]+)")

HB_FIELDS = ["utc", "uptime", "heap", "minheap", "maxblock",
             "rssi", "rx", "tx", "fwd", "fail", "contacts"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=514)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--log", default="gateway.log")
    ap.add_argument("--csv", default="heartbeat.csv")
    ap.add_argument("--quiet", action="store_true", help="do not echo to stdout")
    args = ap.parse_args()

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.bind, args.port))
    except PermissionError:
        print("Port %d needs root. Re-run with sudo, or use --port 5514 "
              "and set the same port on the device." % args.port, file=sys.stderr)
        return 1
    except OSError as e:
        print("Could not bind %s:%d — %s" % (args.bind, args.port, e), file=sys.stderr)
        return 1

    # Announce the addresses the device could be pointed at.
    print("syslog collector listening on %s:%d" % (args.bind, args.port))
    try:
        host_ip = socket.gethostbyname(socket.gethostname())
        print("  set the gateway's log server to: %s" % host_ip)
    except Exception:
        pass
    print("  events -> %s" % args.log)
    print("  heartbeats -> %s" % args.csv)
    print("  Ctrl-C to stop\n")

    new_csv = not os.path.exists(args.csv)
    logf = open(args.log, "a", buffering=1)
    csvf = open(args.csv, "a", newline="", buffering=1)
    writer = csv.writer(csvf)
    if new_csv:
        writer.writerow(HB_FIELDS)

    count = 0
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            raw = data.decode("utf-8", "replace").strip()
            now = datetime.now(timezone.utc).isoformat(timespec="seconds")

            sev, body = "?", raw
            m = PRI_RE.match(raw)
            if m:
                pri = int(m.group(1))
                sev = SEVERITY.get(pri & 7, str(pri & 7))
                body = m.group(2)

            line = "%s %s [%s] %s" % (now, addr[0], sev, body)
            logf.write(line + "\n")
            if not args.quiet:
                print(line, flush=True)

            hb = HB_RE.search(body)
            if hb:
                kv = dict(KV_RE.findall(hb.group(1)))
                writer.writerow([now] + [kv.get(f, "") for f in HB_FIELDS[1:]])

            count += 1
    except KeyboardInterrupt:
        print("\nstopped after %d message(s)" % count)
    finally:
        logf.close()
        csvf.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
