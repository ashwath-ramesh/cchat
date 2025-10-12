#!/usr/bin/env node
const net = require('net');
const { Server } = require('ws');

new Server({ port: 8080 }).on('connection', (ws) => {
  const tcpHost = process.env.SERVER_HOST || 'localhost';
  const tcp = net.connect(3490, tcpHost);

  ws.on('message', (message) => {
    tcp.write(`${message}\n`);
  });

  tcp.on('data', (chunk) => {
    ws.send(chunk.toString());
  });

  const shutdown = () => {
    ws.close();
    tcp.end();
  };

  ws.on('close', () => tcp.end());
  tcp.on('close', () => ws.close());
  ws.on('error', shutdown);
  tcp.on('error', shutdown);
});
