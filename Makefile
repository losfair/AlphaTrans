all:
	make client
	make server
client: client_send.o crc32.o
	$(CXX) -o client_send client_send.o crc32.o
server: server_recv.o crc32.o
	$(CXX) -o server_recv server_recv.o crc32.o
