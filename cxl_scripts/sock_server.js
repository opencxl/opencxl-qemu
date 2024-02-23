const net = require('net');
const fs = require('fs');

const SOCKET_PATH = '/tmp/my_unix_socket';

// Create the Unix socket server
const server = net.createServer(socket => {
  console.log('Client connected');

  // Handle data received from the client
  socket.on('data', data => {
    console.log('Received data:', data.toString());

    // Send a response back to the client
    socket.write('Hello, client!');

    // Close the connection
    socket.end();
  });

  // Handle client connection termination
  socket.on('end', () => {
    console.log('Client disconnected');
  });

  // Handle socket errors
  socket.on('error', err => {
    console.error('Socket error:', err);
  });
});

// Remove the Unix socket file if it already exists
if (fs.existsSync(SOCKET_PATH)) {
  fs.unlinkSync(SOCKET_PATH);
}

// Start listening for client connections on the Unix socket
server.listen(SOCKET_PATH, () => {
  console.log('Unix socket server started');
});