#!/usr/bin/env python3

import urllib.request
import sys

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8005/events"

print(f"[SSE Client] Connecting to {URL}...")
req = urllib.request.Request(URL, headers={"Accept": "text/event-stream"})

try:
    with urllib.request.urlopen(req) as resp:
        print(f"[SSE Client] Connected: status {resp.status}, Content-Type: {resp.headers.get('Content-Type')}")
        for raw_line in resp:
            line = raw_line.decode('utf-8', errors='replace').rstrip('\r\n')
            if line:
                print(f"[SSE Client] << {line}")
            else:
                print("[SSE Client] (end of event)")
except KeyboardInterrupt:
    print("\n[SSE Client] Disconnected.")
except Exception as e:
    print(f"[SSE Client] Error: {e}")
