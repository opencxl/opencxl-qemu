import socket
import os
import threading

SOCKET_PATH = '/tmp/cxl_sock'

# Remove the Unix socket file if it already exists
if os.path.exists(SOCKET_PATH):
    os.remove(SOCKET_PATH)

PCI_VENDOR_ID = 0x00
PCI_DEVICE_ID = 0x02
PCI_COMMAND = 0x04
PCI_REVISION_ID = 0x08
PCI_CLASS_PROG = 0x09
PCI_CLASS_DEVICE = 0x0a


class PciDevice:
    def __init__(self, connection):
        self.connection = connection
        self.config = [0 for _ in range(0x10000)]

    def init_config(self):
        self.config[PCI_VENDOR_ID] = 0x8086
        self.config[PCI_DEVICE_ID] = 0xd93
        self.config[PCI_REVISION_ID] = 1

    def process_cfg_read(self, offset: int, size: int):
        pass

    def process_cfg_write(self, offset: int, size: int, val: int):
        pass


# Function to handle client connections
def handle_client(connection, address):
    print('Client connected:', address)

    while True:
        # Receive data from the client
        try:
            data = connection.recv(4096)
            
            if not data:
                continue

            print('Received data:', data.decode())

            # Check if the connection is still active
            try:
                connection.getpeername()
            except socket.error:
                break
        except:
            pass

    # Close the connection
    connection.close()
    print('Client disconnected:', address)

# Function to accept client connections
def accept_connections(server_socket):
    while True:
        try:
            connection, address = server_socket.accept()
            connection.setblocking(False)

            # Create a new thread for each client connection
            client_thread = threading.Thread(target=handle_client, args=(connection, address))
            client_thread.start()
        except:
            pass

# Create the Unix socket server
server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server_socket.bind(SOCKET_PATH)
server_socket.listen(5)
server_socket.setblocking(False)

print('Unix socket server started')

# Start accepting client connections in a separate thread
accept_thread = threading.Thread(target=accept_connections, args=(server_socket,))
accept_thread.start()
