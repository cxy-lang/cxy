#!/usr/bin/env python3

import os
import socket
import base64
import hashlib
import struct
import time

HOST = os.environ.get("WS_HOST", "127.0.0.1")
PORT = int(os.environ.get("WS_PORT", "8004"))
PATH = os.environ.get("WS_PATH", "/wss")

def make_frame(opcode, payload, mask=True):
    if isinstance(payload, str):
        payload = payload.encode('utf-8')
    length = len(payload)
    header = bytearray()
    header.append(0x80 | (opcode & 0x0F))
    
    mask_bit = 0x80 if mask else 0x00
    if length <= 125:
        header.append(mask_bit | length)
    elif length <= 65535:
        header.append(mask_bit | 126)
        header.extend(struct.pack('!H', length))
    else:
        header.append(mask_bit | 127)
        header.extend(struct.pack('!Q', length))
        
    if mask:
        mask_key = os.urandom(4)
        header.extend(mask_key)
        masked_payload = bytearray(payload[i] ^ mask_key[i % 4] for i in range(length))
        return bytes(header) + bytes(masked_payload)
    else:
        return bytes(header) + payload

def read_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)

def read_frame(sock):
    hdr = read_exact(sock, 2)
    if not hdr:
        return None
    b0, b1 = hdr[0], hdr[1]
    fin = (b0 & 0x80) != 0
    opcode = b0 & 0x0F
    masked = (b1 & 0x80) != 0
    length = b1 & 0x7F
    
    if length == 126:
        raw_len = read_exact(sock, 2)
        if not raw_len: return None
        length = struct.unpack('!H', raw_len)[0]
    elif length == 127:
        raw_len = read_exact(sock, 8)
        if not raw_len: return None
        length = struct.unpack('!Q', raw_len)[0]
        
    mask_key = None
    if masked:
        mask_key = read_exact(sock, 4)
        if not mask_key: return None
        
    payload = read_exact(sock, length) if length > 0 else b""
    if payload is None:
        return None
        
    if masked and mask_key:
        payload = bytes(payload[i] ^ mask_key[i % 4] for i in range(len(payload)))
        
    return opcode, payload

def main():
    print(f"[WS] Connecting to ws://{HOST}:{PORT}{PATH}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    
    key = base64.b64encode(os.urandom(16)).decode('utf-8')
    handshake = (
        f"GET {PATH} HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    s.sendall(handshake.encode('utf-8'))
    
    response = b""
    while b"\r\n\r\n" not in response:
        chunk = s.recv(4096)
        if not chunk:
            print("[WS] Handshake failed: connection closed")
            return
        response += chunk
        
    header_part, extra = response.split(b"\r\n\r\n", 1)
    status_line = header_part.split(b"\r\n")[0].decode('utf-8')
    print(f"[WS] Server handshake response: {status_line}")
    if "101" not in status_line:
        print("[WS] Error: Handshake rejected by server")
        return
        
    print("[WS] Handshake successful! Connected.")
    
    # 1. Send Ping
    print("[WS] Sending Ping...")
    s.sendall(make_frame(0x9, b"ping-data"))
    
    # 2. Send Text Message
    msg = "Hello Cxy WebSocket!"
    print(f"[WS] Sending Message: '{msg}'")
    s.sendall(make_frame(0x1, msg))
    
    # Receive loop
    while True:
        frame = read_frame(s)
        if not frame:
            print("[WS] Connection closed by server")
            break
        opcode, payload = frame
        if opcode == 0x1:
            print(f"[WS] Received Message: '{payload.decode('utf-8')}'")
            # Send second message
            time.sleep(0.3)
            print("[WS] Sending Message: 'Testing 123'")
            s.sendall(make_frame(0x1, "Testing 123"))
        elif opcode == 0xA:
            print(f"[WS] Received Pong: '{payload.decode('utf-8', errors='replace')}'")
        elif opcode == 0x8:
            code = struct.unpack('!H', payload[:2])[0] if len(payload) >= 2 else 1000
            print(f"[WS] Server sent Close: {code}")
            # Echo close
            s.sendall(make_frame(0x8, struct.pack('!H', code)))
            break
            
    s.close()
    print("[WS] Done.")

if __name__ == "__main__":
    main()
