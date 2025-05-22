import paho.mqtt.client as mqtt
import ssl
import time
import threading
import os

# MQTT Broker settings
BROKER = "broker.hivemq.com"
PORT = 8883
TOPIC_SEND = "bootloader-receive"
TOPIC_RECEIVE = "bootloader-send"

# CRC Configuration
CRC_POLY = 0x04C11DB7
CRC_INIT = 0xFFFFFFFF
CRC_XOR_OUT = 0x00000000

# Delay between packets (in seconds)
PACKET_DELAY = 0.50  # 500ms

# Global variables for synchronization
ack_received = threading.Event()
nack_received = threading.Event()
version_received = threading.Event()
unexpected_message = threading.Event()

def crc32(data):
    crc = CRC_INIT
    for byte in data:
        crc ^= (byte << 24) & 0xFFFFFFFF
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ CRC_POLY) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc ^ CRC_XOR_OUT

def pad_bytes(data):
    return b''.join(byte.to_bytes(4, 'big') for byte in data)

def print_packet(packet, description):
    print(f"Sending {description}:")
    print(f"Packet (hex): {packet.hex()}")
    print(f"Packet (bytes): {list(packet)}")
    print()

def send_packet(client, topic, packet, description):
    print_packet(packet, description)
    client.publish(topic, packet)
    time.sleep(PACKET_DELAY)

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Connected with result code {rc}")
    client.subscribe(TOPIC_RECEIVE)

def on_message(client, userdata, msg):
    print(f"Received message on {msg.topic}: {msg.payload.hex()}")
    if msg.payload == b'\xFF':
        print("ACK received")
        ack_received.set()
    elif msg.payload == b'\x01':
        print("NACK received")
        nack_received.set()
    elif len(msg.payload) == 3:
        major, minor, patch = msg.payload
        print(f"Version received: {major}.{minor}.{patch}")
        version_received.set()
    else:
        print(f"Unexpected message received: {msg.payload.hex()}")
        unexpected_message.set()

def request_ack(client, max_retries=5):
    for attempt in range(max_retries):
        ack_request = b'\x04'
        send_packet(client, TOPIC_SEND, ack_request, f"request for ACK signal (attempt {attempt + 1})")
        
        # Wait for a response with a timeout
        if ack_received.wait(timeout=5):
            ack_received.clear()
            return True
        elif nack_received.wait(timeout=5):
            nack_received.clear()
            return False
        elif unexpected_message.wait(timeout=5):
            unexpected_message.clear()
            print(f"Unexpected message received. Retrying ACK request (attempt {attempt + 1}/{max_retries})")
            continue
        else:
            print(f"Timeout waiting for response. Retrying ACK request (attempt {attempt + 1}/{max_retries})")
    
    print(f"Failed to receive a valid response after {max_retries} attempts")
    return False

def sequence_1(client):
    # Prepare and send the initial packet
    command = b'\x00'
    data = b'\x05' + command
    crc = crc32(pad_bytes(data))
    packet = data + crc.to_bytes(4, 'big')
    send_packet(client, TOPIC_SEND, packet, "initial packet (get version command)")
    
    if not request_ack(client, 1):
        print("Failed to receive ACK for initial packet. Aborting.")
        return

    # Send version request
    version_request = b'\x05'
    send_packet(client, TOPIC_SEND, version_request, "request for version")

    # Wait for version with a timeout
    if version_received.wait(timeout=5):
        print("Communication completed successfully")
    else:
        print("Timeout waiting for version")

def sequence_2(client):
    print("\nErase Flash Memory:")
    print("1. Mass Erase")
    print("2. Specific Sector Erase")
    choice = input("Enter your choice (1-2): ")

    if choice == '1':
        # Mass Erase
        start_sector = 0xFF
        num_sectors = 0xFF
    elif choice == '2':
        # Specific Sector Erase
        start_sector = int(input("Enter start sector: "))
        num_sectors = int(input("Enter number of sectors to erase: "))
    else:
        print("Invalid choice. Returning to main menu.")
        return

    # Prepare and send the erase packet
    command = b'\x01'
    data = b'\x07' + command + start_sector.to_bytes(1, 'big') + num_sectors.to_bytes(1, 'big')
    crc = crc32(pad_bytes(data))
    packet = data + crc.to_bytes(4, 'big')
    send_packet(client, TOPIC_SEND, packet, "erase flash memory command")

    if request_ack(client):
        print("Erase command acknowledged")
    else:
        print("Erase command failed")

def sequence_3(client):
    print("\nFlash Program:")
    
    # Step 1: Get the bin or hex file from the user
    while True:
        file_path = input("Enter the path to the bin or hex file: ")
        if os.path.exists(file_path):
            break
        print("File not found. Please try again.")

    # Get version information from the user
    major = int(input("Enter Major Version: "))
    minor = int(input("Enter Minor Version: "))
    patch = int(input("Enter Patch Version: "))
    
    # Get start address from the user
    start_address = int(input("Enter start address of the program (in hexadecimal): "), 16)

    # Read the file
    with open(file_path, 'rb') as file:
        file_content = file.read()

    # Get the size of the program
    program_size = len(file_content)

    # Step 2: Send initial packet
    command = b'\x02'  # 0x02 for flash program
    data = (b'\x10' +  # Length (16 bytes)
            command +
            major.to_bytes(1, 'big') +
            minor.to_bytes(1, 'big') +
            patch.to_bytes(1, 'big') +
            start_address.to_bytes(4, 'big') +
            program_size.to_bytes(4, 'big'))
    crc = crc32(pad_bytes(data))
    initial_packet = data + crc.to_bytes(4, 'big')

    # Function to send a packet and request ACK
    def send_and_confirm(packet, description):
        max_retries = 3
        for attempt in range(max_retries):
            send_packet(client, TOPIC_SEND, packet, f"{description} (attempt {attempt + 1})")
            
            ack_received.clear()
            nack_received.clear()
            
            ack_request = b'\x04'
            send_packet(client, TOPIC_SEND, ack_request, f"request for ACK signal (attempt {attempt + 1})")
            
            if ack_received.wait(timeout=5):
                print(f"ACK received for {description}")
                return True
            elif nack_received.wait(timeout=5):
                print(f"NACK received. Resending {description}")
                continue
            else:
                print(f"Timeout waiting for response. Retrying {description}")
        
        print(f"Failed to receive ACK after {max_retries} attempts. Aborting.")
        return False

    # Send initial packet
    if not send_and_confirm(initial_packet, "initial flash program packet"):
        return

    # Step 4 and 5: Send file content in chunks
    chunk_size = 252
    for i in range(0, len(file_content), chunk_size):
        chunk = file_content[i:i+chunk_size]
        crc = crc32(pad_bytes(chunk))
        packet = chunk + crc.to_bytes(4, 'big')
        
        if not send_and_confirm(packet, f"file chunk {i//chunk_size + 1}"):
            return

    # Step 6: Print success message
    print("Flash programming completed successfully!")

def sequence_4(client):
    print("\nJump to Address in Flash Memory:")
    print("1. Jump to Main Application")
    print("2. Jump to Specific Address")
    choice = input("Enter your choice (1-2): ")

    if choice == '1':
        # Jump to Main Application
        address = 0xFFFFFFFF
        boot_choice = input("Do you want to boot into the main application next time? (y/n): ").lower()
        next_time_boot = 0xFF if boot_choice == 'y' else 0xAA
    elif choice == '2':
        # Jump to Specific Address
        address = int(input("Enter the address to jump to (in hexadecimal): "), 16)
        next_time_boot = 0xFF  # Always needed for specific address
    else:
        print("Invalid choice. Returning to main menu.")
        return

    # Prepare and send the jump packet
    command = b'\x03'
    data = (b'\x0A' +  # Length (10 bytes)
            command +
            address.to_bytes(4, 'big') +
            next_time_boot.to_bytes(1, 'big'))
    crc = crc32(pad_bytes(data))
    packet = data + crc.to_bytes(4, 'big')
    
    send_packet(client, TOPIC_SEND, packet, "jump to address command")

    if request_ack(client):
        print("Jump command acknowledged")
    else:
        print("Jump command failed")


def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    # Set up SSL/TLS
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLSv1_2)
    client.tls_insecure_set(True)  # For self-signed certificates

    client.connect(BROKER, PORT, 60)

    # Start the MQTT client loop in a separate thread
    client.loop_start()

    # Wait for the connection to be established
    time.sleep(1)

    while True:
        print("\nSelect a sequence to run:")
        print("1. Get Version through MQTT")
        print("2. Erase Flash Memory")
        print("3. Flash Program")
        print("4. Jump to Address in Flash Memory")
        print("0. Exit")

        choice = input("Enter your choice (0-4): ")

        if choice == '1':
            sequence_1(client)
        elif choice == '2':
            sequence_2(client)
        elif choice == '3':
            sequence_3(client)
        elif choice == '4':
            sequence_4(client)
        elif choice == '0':
            break
        else:
            print("Invalid choice. Please try again.")

        # Reset event flags
        ack_received.clear()
        nack_received.clear()
        version_received.clear()
        unexpected_message.clear()

    # Stop the MQTT client loop and disconnect
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()