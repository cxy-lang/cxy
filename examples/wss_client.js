#!/usr/bin/env node

const http = require('node:http');
const crypto = require('node:crypto');

const HOST = process.env.WS_HOST || '127.0.0.1';
const PORT = Number(process.env.WS_PORT || 8004);
const PATH = process.env.WS_PATH || '/wss';

function createMaskedFrame(opcode, payload) {
    const buf = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
    const len = buf.length;
    let header;
    if (len <= 125) {
        header = Buffer.alloc(2 + 4);
        header[0] = 0x80 | opcode;
        header[1] = 0x80 | len;
    } else if (len <= 65535) {
        header = Buffer.alloc(4 + 4);
        header[0] = 0x80 | opcode;
        header[1] = 0x80 | 126;
        header.writeUInt16BE(len, 2);
    } else {
        header = Buffer.alloc(10 + 4);
        header[0] = 0x80 | opcode;
        header[1] = 0x80 | 127;
        header.writeBigUInt64BE(BigInt(len), 2);
    }

    const maskOffset = header.length - 4;
    const maskKey = crypto.randomBytes(4);
    maskKey.copy(header, maskOffset);

    const maskedPayload = Buffer.alloc(len);
    for (let i = 0; i < len; i++) {
        maskedPayload[i] = buf[i] ^ maskKey[i % 4];
    }

    return Buffer.concat([header, maskedPayload]);
}

function parseFrames(buffer, onMessage, onPong, onClose) {
    let offset = 0;
    while (offset + 2 <= buffer.length) {
        const b0 = buffer[offset];
        const b1 = buffer[offset + 1];
        const opcode = b0 & 0x0f;
        const masked = (b1 & 0x80) !== 0;
        let payloadLen = b1 & 0x7f;
        let headerLen = 2;

        if (payloadLen === 126) {
            if (offset + 4 > buffer.length) break;
            payloadLen = buffer.readUInt16BE(offset + 2);
            headerLen = 4;
        } else if (payloadLen === 127) {
            if (offset + 10 > buffer.length) break;
            payloadLen = Number(buffer.readBigUInt64BE(offset + 2));
            headerLen = 10;
        }

        if (masked) headerLen += 4;
        if (offset + headerLen + payloadLen > buffer.length) break;

        let payload = buffer.subarray(offset + headerLen, offset + headerLen + payloadLen);
        if (masked) {
            const mask = buffer.subarray(offset + headerLen - 4, offset + headerLen);
            const unmasked = Buffer.alloc(payloadLen);
            for (let i = 0; i < payloadLen; i++) {
                unmasked[i] = payload[i] ^ mask[i % 4];
            }
            payload = unmasked;
        }

        offset += headerLen + payloadLen;

        if (opcode === 0x1) {
            onMessage(payload.toString('utf8'));
        } else if (opcode === 0xa) {
            onPong(payload);
        } else if (opcode === 0x8) {
            const code = payload.length >= 2 ? payload.readUInt16BE(0) : 1000;
            const reason = payload.length > 2 ? payload.subarray(2).toString('utf8') : '';
            onClose(code, reason);
        }
    }
    return buffer.subarray(offset);
}

function run() {
    const key = crypto.randomBytes(16).toString('base64');
    const req = http.request({
        host: HOST,
        port: PORT,
        path: PATH,
        headers: {
            'Connection': 'Upgrade',
            'Upgrade': 'websocket',
            'Sec-WebSocket-Key': key,
            'Sec-WebSocket-Version': '13'
        }
    });

    req.on('upgrade', (res, socket, head) => {
        console.log(`[WS] Connected to ws://${HOST}:${PORT}${PATH}`);
        let buf = head || Buffer.alloc(0);

        function handleMessage(msg) {
            console.log(`[WS] Received message: "${msg}"`);
        }

        function handlePong(data) {
            console.log(`[WS] Received pong: "${data.toString('utf8')}"`);
        }

        function handleClose(code, reason) {
            console.log(`[WS] Closed by server: ${code} (${reason})`);
            socket.end();
            process.exit(0);
        }

        socket.on('data', (chunk) => {
            buf = Buffer.concat([buf, chunk]);
            buf = parseFrames(buf, handleMessage, handlePong, handleClose);
        });

        socket.on('end', () => {
            console.log('[WS] Socket disconnected');
        });

        // 1. Send Ping
        console.log('[WS] Sending ping...');
        socket.write(createMaskedFrame(0x9, 'ping-payload'));

        // 2. Send Text message
        console.log('[WS] Sending message: "Hello Cxy WebSocket!"');
        socket.write(createMaskedFrame(0x1, 'Hello Cxy WebSocket!'));

        // 3. Send second text message
        setTimeout(() => {
            console.log('[WS] Sending message: "Ping-Pong Test 2"');
            socket.write(createMaskedFrame(0x1, 'Ping-Pong Test 2'));
        }, 500);

        // 4. Close connection cleanly
        setTimeout(() => {
            console.log('[WS] Sending close frame...');
            const closePayload = Buffer.alloc(2);
            closePayload.writeUInt16BE(1000, 0);
            socket.write(createMaskedFrame(0x8, closePayload));
        }, 1200);
    });

    req.on('error', (err) => {
        console.error('[WS] Connection error:', err.message);
        process.exit(1);
    });

    req.end();
}

run();
