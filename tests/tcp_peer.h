#pragma once
struct Peer {
 Socket socketValue{INVALID_SOCKET};
 explicit Peer(unsigned short port) {
  socketValue=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(socketValue!=INVALID_SOCKET);
  DWORD timeout=2000;setsockopt(socketValue,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(0x7f000001);
  CHECK(connect(socketValue,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
 }
 ~Peer(){Close();}
 void Close(){if(socketValue!=INVALID_SOCKET){closesocket(socketValue);socketValue=INVALID_SOCKET;}}
 void Send(unsigned char proto,const Bytes& body={}) {
  Bytes bytes{static_cast<unsigned char>((body.size()+1)&255),static_cast<unsigned char>((body.size()+1)>>8),proto};
  bytes.insert(bytes.end(),body.begin(),body.end());
  std::size_t sent=0;while(sent<bytes.size()){int n=send(socketValue,reinterpret_cast<const char*>(bytes.data()+sent),bytes.size()-sent,0);CHECK(n>0);sent+=n;}
 }
 template<class T> void SendStruct(unsigned char proto,const T& data) {
  auto p=reinterpret_cast<const unsigned char*>(&data);Send(proto,Bytes(p,p+sizeof data));
 }
 void Envelope(const undo::Envelope& e){Send(undo::RoomOuterOpcode,undo::Encode(e));}
 Bytes Read() {
  unsigned char prefix[2];if(!ReadExact(prefix,2))return {};
  unsigned size=prefix[0]|unsigned(prefix[1])<<8;CHECK(size);
  Bytes packet(size);CHECK(ReadExact(packet.data(),packet.size()));return packet;
 }
 bool ReadExact(unsigned char* bytes,std::size_t size) {
  for(std::size_t have=0;have<size;){int got=recv(socketValue,reinterpret_cast<char*>(bytes+have),size-have,0);if(!got)return false;CHECK(got>0);have+=got;}return true;
 }
 undo::Envelope NextHello() {
  for(int i=0;i<20;++i){auto packet=Read();CHECK(!packet.empty());if(packet[0]==undo::RoomOuterOpcode)return undo::Decode(Bytes(packet.begin()+1,packet.end()));}
  throw std::runtime_error("Missing server capability response");
 }
 void Rejected() {
  for(int i=0;i<20;++i){auto packet=Read();if(packet.empty())return;CHECK(packet[0]!=STOC_JOIN_GAME);}
  throw std::runtime_error("Legacy/incompatible peer was not rejected");
 }
};
