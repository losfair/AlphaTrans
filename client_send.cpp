#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "crc32.h"
using namespace std;
int port=6789;

#define SEND_BUFFER_SIZE 1024
#define PACKETS_PER_BLOCK 1024
#define EMPTY_PACKETS_NUM 100

int delay_us=200;

string ipAddr;

struct DataPacket {
	unsigned id;
	char data[SEND_BUFFER_SIZE];
	unsigned crc32_value;
};

vector<char*> sendBuffer;

DataPacket makeDataPacket(unsigned id, char *data) {
	DataPacket ret;
	ret.id=id;
	memcpy(ret.data,data,SEND_BUFFER_SIZE);
	ret.crc32_value=crc32((unsigned char*)ret.data,SEND_BUFFER_SIZE);
	return ret;
}

bool pkt_id_compare(const DataPacket &a,const DataPacket &b) {
	return a.id<b.id;
}

void destroySendBuffer() {
	for(int i=0;i<sendBuffer.size();i++) {
		delete[] sendBuffer[i];
	}
	sendBuffer.clear();
}

unsigned readFileData() {
	istream& inFile=cin;

	unsigned length=0;

	destroySendBuffer();
	
	for(int i=0;i<PACKETS_PER_BLOCK && !inFile.eof();i++) {
		char *buf=new char [SEND_BUFFER_SIZE];
		for(int i=0;i<SEND_BUFFER_SIZE;i++) buf[i]=0;
		inFile.read(buf,SEND_BUFFER_SIZE);
		length+=inFile.gcount();
		sendBuffer.push_back(buf);
	}
	return length;
}

int connectControl() {
int cfd;
int recbytes;
int sin_size;
char buffer[1024]={0};
struct sockaddr_in s_add,c_add;
unsigned short portnum=port;

cfd = socket(AF_INET, SOCK_STREAM, 0);
if(-1 == cfd)
{
    return -1;
}
bzero(&s_add,sizeof(struct sockaddr_in));
s_add.sin_family=AF_INET;
s_add.sin_addr.s_addr= inet_addr(ipAddr.c_str()); 
s_add.sin_port=htons(portnum);

if(-1 == connect(cfd,(struct sockaddr *)(&s_add), sizeof(struct sockaddr)))
{
    return -1;
}

return cfd;
}

double sendBlock(int tcpConn) {
    int socket_descriptor; 
    int iter=0;
    char buf[80];
    struct sockaddr_in address;

    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=inet_addr(ipAddr.c_str());
    address.sin_port=htons(port);

    socket_descriptor=socket(AF_INET,SOCK_DGRAM,0);

    unsigned length=readFileData();

    cerr<<"Length: "<<length<<endl;

    cerr<<"Sending "<<sendBuffer.size()<<"*"<<SEND_BUFFER_SIZE<<" bytes of data"<<endl;

    write(tcpConn,&length,sizeof(unsigned));

    usleep(delay_us);

    vector<DataPacket> packets;

    for(int i=0;i<sendBuffer.size();i++) {
	packets.push_back(makeDataPacket(i,sendBuffer[i]));
    }

    unsigned sendBuffer_size=packets.size();
    write(tcpConn,&sendBuffer_size,sizeof(unsigned));
    
    unsigned totalPackets=0;
    unsigned totalLoss=0;

    send_start:

    for(int i=0;i<packets.size();i++) {
	sendto(socket_descriptor,&packets[i],sizeof(DataPacket),0,(struct sockaddr *)&address,sizeof(address));
	totalPackets++;
	usleep(delay_us);
//	cout<<"Packet with id "<<packets[i].id<<" sent"<<endl;
    }

    cerr<<"Sending empty packets"<<endl;

    usleep(delay_us*3);

    for(int i=0;i<EMPTY_PACKETS_NUM;i++) {
	DataPacket emptyPkt;
	emptyPkt.id=0x1fffffff;
	sendto(socket_descriptor,&emptyPkt,sizeof(emptyPkt),0,(struct sockaddr *)&address,sizeof(address));
	usleep(delay_us);
//	cout<<"Empty packet with id "<<emptyPkt.id<<" sent"<<endl;
    }

    unsigned lostPackets_size;
    vector<unsigned> lostPackets;

    read(tcpConn,&lostPackets_size,sizeof(unsigned));

    cerr<<"[*] lostPackets_size: "<<lostPackets_size<<endl;

    if(lostPackets_size>0) {
	for(int i=0;i<lostPackets_size;i++) {
		unsigned lp_buf;
		read(tcpConn,&lp_buf,sizeof(unsigned));
		lostPackets.push_back(lp_buf);
	}
    }

    cerr<<"Lost packets:"<<endl;
    for(int i=0;i<lostPackets_size;i++) cerr<<"[X] "<<lostPackets[i]<<endl;
    totalLoss+=lostPackets_size;

    if(lostPackets_size==0) goto finish_sending;
   
//    cout<<"CRC32 values:"<<endl;
//    sort(packets.begin(),packets.end(),pkt_id_compare);
//    for(int i=0;i<packets.size();i++) {
//	cout<<"#"<<packets[i].id<<" "<<packets[i].crc32_value<<endl;
//    }
 
    packets.clear();
    for(int i=0;i<lostPackets_size;i++) {
	packets.push_back(makeDataPacket(lostPackets[i],sendBuffer[lostPackets[i]]));
    }
    goto send_start;

    finish_sending:
    destroySendBuffer();

    close(socket_descriptor);

    return (double)totalLoss/(double)totalPackets;
}

int main(int argc, char *argv[]) {
    if(argc!=2) {
	cerr<<"Bad arguments"<<endl;
	exit(1);
    }

    ipAddr=argv[1];

    cerr<<"Connecting control"<<endl;
    int tcpConn=connectControl();
    if(tcpConn<=0) {
	cerr<<"Unable to establish control connection"<<endl;
	return 1;
    }

    double lossSum=0.0;
    unsigned pktCount=0;

    while(!cin.eof()) {
	cerr<<"Sending block"<<endl;
	unsigned sigContinue=0x20001000;
	write(tcpConn,&sigContinue,sizeof(unsigned));
	double loss=sendBlock(tcpConn);
	cerr<<"Loss: "<<loss<<endl;
	lossSum+=loss;
	pktCount++;
	if(loss>0.10 && delay_us<1500) delay_us+=50;
	else if(loss<0.10 && delay_us>=100) delay_us-=50;
	cerr<<"Current delay_us: "<<delay_us<<endl;
    }

    cerr<<"Average loss: "<<lossSum/pktCount<<endl;

    unsigned sigTerminate=0x20002000;
    write(tcpConn,&sigTerminate,sizeof(unsigned));

    close(tcpConn);

    return 0;
}
