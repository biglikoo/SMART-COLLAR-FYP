import socket

UDP_IP = "0.0.0.0"
UDP_PORT = 8081 # The dedicated debug port

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print("📡 WIRELESS SERIAL MONITOR ONLINE... Listening for ESP32...")

while True:
    data, addr = sock.recvfrom(1024) 
    print(f"[ESP32] {data.decode('utf-8')}")