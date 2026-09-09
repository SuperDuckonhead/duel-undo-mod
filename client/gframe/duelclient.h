#ifndef DUELCLIENT_H
#define DUELCLIENT_H
#include "undo/single_undo.h"

#include <vector>
#include "bufferio.h"
#include "deck.h"
#include "network.h"

namespace undo { struct RoomConfig; }
namespace ygo {
class RoomClient;

#define CONNECT_STATE_NONE			0x0
#define CONNECT_STATE_CONNECTING	0x1
#define CONNECT_STATE_CONNECTED		0x2
#define CONNECT_STATE_JOINED		0x4

#define CLIENT_CLOSE_REASON_NONE	0
#define CLIENT_CLOSE_REASON_STOP	1
#define CLIENT_CLOSE_REASON_EXIT	2

struct DuelPromptContext {int selectHint{},unselectHint{},lastHint{};std::array<wchar_t,256> event{};};
class DuelClient {
private:
	static bufferevent* client_bev;
	static unsigned char duel_client_write[SIZE_NETWORK_BUFFER];
	static int WriteBufferEvent(bufferevent* bufev, const void* data, size_t size);

public:
	static unsigned char selftype;
 static std::shared_ptr<RoomClient> Room();
 static void ConfigureRoom(std::shared_ptr<const undo::RoomConfig>);
 static void RoomPoll(EventSocket, short, void*);
 static void SendLegacyPacket(unsigned char, const void*, size_t);
 static void HandleLegacySTOC(unsigned char*, size_t);
	static bool StartClient(unsigned int ip, unsigned short port, bool create_game = true);
	static void ConnectTimeout(EventSocket fd, short events, void* arg);
	static void StopClient(unsigned reason = CLIENT_CLOSE_REASON_STOP);
	static void ClientRead(bufferevent* bev, void* ctx);
	static void ClientEvent(bufferevent* bev, short events, void* ctx);
	static void ClientThread();
	static void HandleSTOCPacketLan(unsigned char* data, size_t len);
	static bool ClientAnalyze(unsigned char* msg, size_t len);
	static void SwapField();
	static void SetResponseI(int32_t respI);
	static void SetResponseB(void* respB, size_t len);
	static void SendResponse();
 static void SendResponse(const undo::InputSubmission&);
 static undo::InputSubmission CaptureResponse();
 static void ClearPendingResponse();
 static DuelPromptContext CapturePromptContext();
 static void RestorePromptContext(const DuelPromptContext&) noexcept;
	static void SendUpdateDeck(const Deck& deck);
	static void SendPacketToServer(unsigned char proto) { SendLegacyPacket(proto,nullptr,0); }
	template<typename ST> static void SendPacketToServer(unsigned char proto,const ST& st) {
        static_assert(sizeof(ST)<=MAX_DATA_SIZE,"Packet size is too large.");
        SendLegacyPacket(proto,&st,sizeof(ST));
    }
    static void SendBufferToServer(unsigned char proto,void* buffer,size_t len) {
        if(len>MAX_DATA_SIZE)return;
        SendLegacyPacket(proto,buffer,len);
    }

	static std::vector<HostPacket> hosts;
	static void BeginRefreshHost();
	static int RefreshThread(event_base* broadev);
	static void BroadcastReply(EventSocket fd, short events, void* arg);

	static unsigned int ResolveHostName(const char* hostname, const char* port);
};

}

#endif //DUELCLIENT_H
