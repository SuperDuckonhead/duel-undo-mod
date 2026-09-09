#include "config.h"
#include "netserver.h"
#include "single_duel.h"
#include "tag_duel.h"
#include "deck_manager.h"
#include "mysocket.h"
#include <thread>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <memory>
#include <limits>
#include <stdexcept>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

namespace ygo {

namespace{
	std::unordered_map<bufferevent*, DuelPlayer> users{};
	unsigned short server_port{};
	event_base* net_evbase{};
	event* broadcast_ev{};
	event* duel_etimer{};
	evconnlistener* listener{};
	DuelMode* duel_mode{};
	bool broadcast_enabled{};
	unsigned char net_server_read[SIZE_NETWORK_BUFFER]{};
    std::unique_ptr<undo::RoomAdmission> room_admission;
    std::atomic<bool> server_running{false};
    event* undo_poll{};
    uint64_t next_endpoint{};
    int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void RejectUndoPeer(DuelPlayer* dp) {
        STOC_ErrorMsg error;error.msg=ERRMSG_JOINERROR;error.code=0;
        NetServer::SendPacketToPlayer(dp,STOC_ERROR_MSG,error);
        if(dp->game) dp->game->LeaveGame(dp); else NetServer::DisconnectPlayer(dp);
    }
    void UndoPoll(EventSocket,short,void*) {
        const auto now=NowMs();
        std::vector<bufferevent*> expired;
        for(const auto& pair:users)
            if(!pair.second.undoPeer.ready && now-pair.second.connectedAtMs>=30000)
                expired.push_back(pair.first);
        for(auto* bev:expired) {
            const auto it=users.find(bev);
            if(it!=users.end()) RejectUndoPeer(&it->second);
        }
        if(duel_mode) {
            try { duel_mode->PollUndo(); }
            catch(...) { NetServer::StopServer(); }
        }
    }


	void DuelTimer(EventSocket, short, void* arg) {
		static_cast<DuelMode*>(arg)->TimerTick();
	}
}

unsigned char NetServer::net_server_write[SIZE_NETWORK_BUFFER]{};
size_t NetServer::last_sent{};
bufferevent* NetServer::disconnecting_bev = nullptr;

int NetServer::WriteBufferEvent(bufferevent* bufev, const void* data, size_t size) {
	return bufferevent_write(bufev, data, size);
}

void NetServer::DeliverPrepared(DuelPlayer* dp,const unsigned char* packet,size_t size) {
    if(!dp || size<3) return;
    // Route before checking bev: a host-created virtual bot endpoint has no TCP socket.
    if(dp->game && dp->game->RoutePacket(dp,packet[2],packet+3,size-3)) return;
    if(CanWriteToPlayer(dp)) WriteBufferEvent(dp->bev,packet,size);
}
bool NetServer::SendUndoToPlayer(DuelPlayer* dp,const undo::Envelope& envelope) {
    auto body=undo::Encode(envelope);
    if(!CanWriteToPlayer(dp) || body.size()>MAX_DATA_SIZE) return false;
    std::vector<unsigned char> packet;
    packet.reserve(body.size()+3);
    BufferIO::VectorWrite<uint16_t>(packet,static_cast<uint16_t>(body.size()+1));
    packet.push_back(STOC_UNDO);packet.insert(packet.end(),body.begin(),body.end());
    return WriteBufferEvent(dp->bev,packet.data(),packet.size())==0;
}
bool NetServer::IsRunning() { return server_running.load(); }
const undo::RoomAdmission* NetServer::Admission() { return room_admission.get(); }

bool NetServer::StartServer(unsigned short port, unsigned int ip, unsigned short* out_actual_port, bool enable_broadcast, const undo::Hello* undo_capability) {
	if(net_evbase)
		return false;
	net_evbase = event_base_new();
	if(!net_evbase)
		return false;
	sockaddr_in sin;
	std::memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(ip);
	sin.sin_port = htons(port);
	listener = evconnlistener_new_bind(net_evbase, ServerAccept, nullptr,
	                                   LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, (sockaddr*)&sin, sizeof(sin));
	if(!listener) {
		event_base_free(net_evbase);
		net_evbase = nullptr;
		return false;
	}
	sockaddr_in bound_addr;
	std::memset(&bound_addr, 0, sizeof bound_addr);
	socklen_t bound_addr_len = sizeof bound_addr;
	if(getsockname(evconnlistener_get_fd(listener), (sockaddr*)&bound_addr, &bound_addr_len) == SOCKET_RESULT_ERROR) {
		evconnlistener_free(listener);
		listener = nullptr;
		event_base_free(net_evbase);
		net_evbase = nullptr;
		return false;
	}
	server_port = ntohs(bound_addr.sin_port);
	if(out_actual_port)
		*out_actual_port = server_port;
	broadcast_enabled = enable_broadcast;
	evconnlistener_set_error_cb(listener, ServerAcceptError);
	    try {
        if(undo_capability) {
            // getsockname establishes the real binding; a claimed mode cannot grant it.
            const bool loopback=ntohl(bound_addr.sin_addr.s_addr)==0x7f000001;
            room_admission=std::make_unique<undo::RoomAdmission>(*undo_capability,loopback);
            undo_poll=event_new(net_evbase,-1,EV_PERSIST,UndoPoll,nullptr);
            if(!undo_poll) throw std::runtime_error("Cannot create undo control poll");
            timeval interval{0,20000};
            if(event_add(undo_poll,&interval)!=0) throw std::runtime_error("Cannot start undo control poll");
        }
        next_endpoint=0;
        server_running=true;
        std::thread(ServerThread).detach();
    } catch(...) {
        server_running=false;
        if(undo_poll) event_free(undo_poll);
        undo_poll=nullptr;room_admission.reset();
        evconnlistener_free(listener);listener=nullptr;
        event_base_free(net_evbase);net_evbase=nullptr;
        return false;
    }
	return true;
}
bool NetServer::StartBroadcast() {
	if(!net_evbase || !broadcast_enabled)
		return false;
	Socket udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int opt = 1;
	setsockopt(udp, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof opt);
	setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof opt);
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7920);
	addr.sin_addr.s_addr = 0;
	if(bind(udp, (sockaddr*)&addr, sizeof(addr)) == SOCKET_RESULT_ERROR) {
		CloseSocket(udp);
		return false;
	}
	broadcast_ev = event_new(net_evbase, udp, EV_READ | EV_PERSIST, BroadcastEvent, nullptr);
	event_add(broadcast_ev, nullptr);
	return true;
}
void NetServer::StopServer() {
	if(!net_evbase)
		return;
	if(duel_mode)
		duel_mode->EndDuel();
	event_base_loopexit(net_evbase, 0);
}
void NetServer::StopBroadcast() {
	if(!net_evbase || !broadcast_ev)
		return;
	event_del(broadcast_ev);
	EventSocket fd;
	event_get_assignment(broadcast_ev, 0, &fd, 0, 0, 0);
	evutil_closesocket(fd);
	event_free(broadcast_ev);
	broadcast_ev = nullptr;
}
void NetServer::StopListen() {
	evconnlistener_disable(listener);
	StopBroadcast();
}
void NetServer::StartDuelTimer() {
	if(!duel_etimer)
		return;
	timeval timeout = { 1, 0 };
	event_add(duel_etimer, &timeout);
}
void NetServer::StopDuelTimer() {
	if(duel_etimer)
		event_del(duel_etimer);
}
void NetServer::BroadcastEvent(EventSocket fd, short events, void* arg) {
	sockaddr_in bc_addr;
	socklen_t sz = sizeof(sockaddr_in);
	char buf[256];
	int ret = recvfrom(fd, buf, 256, 0, (sockaddr*)&bc_addr, &sz);
	if(ret == -1)
		return;
	HostRequest packet;
	std::memcpy(&packet, buf, sizeof packet);
	const HostRequest* pHR = &packet;
	if(pHR->identifier == NETWORK_CLIENT_ID) {
		sockaddr_in sockTo;
		sockTo.sin_addr.s_addr = bc_addr.sin_addr.s_addr;
		sockTo.sin_family = AF_INET;
		sockTo.sin_port = htons(7921);
		HostPacket hp;
		hp.identifier = NETWORK_SERVER_ID;
		hp.port = server_port;
		hp.version = PRO_VERSION;
		hp.host = duel_mode->host_info;
		BufferIO::CopyCharArray(duel_mode->name, hp.name);
		sendto(fd, (const char*)&hp, sizeof(HostPacket), 0, (sockaddr*)&sockTo, sizeof(sockTo));
	}
}
void NetServer::ServerAccept(evconnlistener* listener, EventSocket fd, sockaddr* address, int socklen, void* ctx) {
    if(next_endpoint==std::numeric_limits<uint64_t>::max()) { evutil_closesocket(fd);return; }
    const bool loopback=address && address->sa_family==AF_INET && socklen>=sizeof(sockaddr_in) &&
        (ntohl(reinterpret_cast<sockaddr_in*>(address)->sin_addr.s_addr)&0xff000000u)==0x7f000000u;
    if(room_admission && room_admission->Capability().mode==undo::RoomMode::LoopbackFree && !loopback) {
        evutil_closesocket(fd);return;
    }
	bufferevent* bev = bufferevent_socket_new(net_evbase, fd, BEV_OPT_CLOSE_ON_FREE);
	DuelPlayer dp;
    dp.endpointId=++next_endpoint;dp.connectedAtMs=NowMs();dp.undoPeer.loopback=loopback;
	dp.name[0] = 0;
	dp.type = 0xff;
	dp.bev = bev;
	if(!bev) { evutil_closesocket(fd);return; }
    users[bev] = dp;
	bufferevent_setcb(bev, ServerEchoRead, nullptr, ServerEchoEvent, nullptr);
	bufferevent_enable(bev, EV_READ);
}
void NetServer::ServerAcceptError(evconnlistener* listener, void* ctx) {
	event_base_loopexit(net_evbase, 0);
}
/*
* packet_len: 2 bytes
* proto: 1 byte
* [data]: (packet_len - 1) bytes
*/
void NetServer::ServerEchoRead(bufferevent *bev, void *ctx) {
	evbuffer* input = bufferevent_get_input(bev);
	size_t len = evbuffer_get_length(input);
	if (len < 2)
		return;
	uint16_t packet_len = 0;
	while (len >= 2) {
		evbuffer_copyout(input, &packet_len, sizeof packet_len);
		if (len < packet_len + 2)
			break;
		int read_len = evbuffer_remove(input, net_server_read, packet_len + 2);
		if (read_len > 2)
			HandleCTOSPacket(&users[bev], &net_server_read[2], read_len - 2);
		if(users.find(bev)==users.end()) return; // HandleCTOS may have disconnected and freed its input buffer.
        len -= packet_len + 2;
	}
}
void NetServer::ServerEchoEvent(bufferevent* bev, short events, void* ctx) {
	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		DuelPlayer* dp = &users[bev];
		DuelMode* dm = dp->game;
		auto* prev_disconnect = disconnecting_bev;
		disconnecting_bev = bev;
		if(dm)
			dm->LeaveGame(dp);
		else
			DisconnectPlayer(dp);
		disconnecting_bev = prev_disconnect;
	}
}
void NetServer::ServerThread() {
	event_base_dispatch(net_evbase);
	for(auto bit = users.begin(); bit != users.end(); ++bit) {
		bufferevent_disable(bit->first, EV_READ);
		bufferevent_free(bit->first);
	}
	users.clear();
	evconnlistener_free(listener);
	listener = nullptr;
	if(broadcast_ev) {
		EventSocket fd;
		event_get_assignment(broadcast_ev, 0, &fd, 0, 0, 0);
		evutil_closesocket(fd);
		event_free(broadcast_ev);
		broadcast_ev = nullptr;
	}
	if(duel_etimer)
		event_free(duel_etimer);
	duel_etimer = nullptr;
	if(duel_mode)
		delete duel_mode;
	duel_mode = nullptr;
	if(undo_poll) event_free(undo_poll);
    undo_poll=nullptr;room_admission.reset();
	event_base_free(net_evbase);
	net_evbase = nullptr;
    server_running=false;
}
void NetServer::DisconnectPlayer(DuelPlayer* dp) {
	auto bit = users.find(dp->bev);
	if(bit != users.end()) {
		if(dp->game) {
			dp->game->OnPlayerDisconnected(dp);
			dp->game = nullptr;
		}
		bufferevent_flush(dp->bev, EV_WRITE, BEV_FLUSH);
		bufferevent_disable(dp->bev, EV_READ);
		bufferevent_free(dp->bev);
		dp->bev = nullptr;
		users.erase(bit);
	}
}
void NetServer::HandleCTOSPacket(DuelPlayer* dp, unsigned char* data, size_t len) {
    if(!dp || !data || !len) return;
    if(data[0]==CTOS_UNDO) {
        if(!room_admission) return; // Ordinary servers retain their old protocol.
        try {
            const auto message=undo::Decode(undo::Bytes(data+1,data+len));
            if(message.kind==undo::WireKind::Hello) {
                if(room_admission->Receive(dp->undoPeer,message)==undo::HandshakeResult::Confirmed)
                    SendUndoToPlayer(dp,room_admission->Confirmation());
            } else if(dp->undoPeer.ready && dp->game) {
                dp->game->ReceiveUndo(dp,message);
            } else throw std::invalid_argument("Undo capability not confirmed");
        } catch(...) { RejectUndoPeer(dp); }
        return;
    }

	auto pdata = data;
	unsigned char pktType = BufferIO::Read<uint8_t>(pdata);
    if(room_admission) {
        if((pktType==CTOS_HS_READY || pktType==CTOS_HS_NOTREADY || pktType==CTOS_HS_START) && !dp->undoPeer.ready) return;
        if(pktType==CTOS_HS_TOOBSERVER) return; // Undo rooms have exactly two duel participants.
        if(dp->game && dp->game->HasActiveDuel() &&
           (pktType==CTOS_RESPONSE || pktType==CTOS_TIME_CONFIRM || pktType==CTOS_SURRENDER || pktType==CTOS_CHAT)) return;
        if(pktType==CTOS_HS_START && dp->game && !dp->game->SupportsUndo()) return;
    }
	if((pktType != CTOS_SURRENDER) && (pktType != CTOS_CHAT) && (dp->state == 0xff || (dp->state && dp->state != pktType)))
		return;
	switch(pktType) {
	case CTOS_RESPONSE: {
		if(!dp->game || !duel_mode->HasActiveDuel())
			return;
		if (len < 1 + sizeof(unsigned char))
			return;
		duel_mode->GetResponse(dp, pdata, len - 1);
		break;
	}
	case CTOS_TIME_CONFIRM: {
		if(!dp->game || !duel_mode->HasActiveDuel())
			return;
		duel_mode->TimeConfirm(dp);
		break;
	}
	case CTOS_CHAT: {
		if(!dp->game)
			return;
		if (len < 1 + sizeof(uint16_t) * 1)
			return;
		if (len > 1 + sizeof(uint16_t) * LEN_CHAT_MSG)
			return;
		if ((len - 1) % sizeof(uint16_t))
			return;
		duel_mode->Chat(dp, pdata, len - 1);
		break;
	}
	case CTOS_UPDATE_DECK: {
		if(!dp->game)
			return;
		if (len < 1 + sizeof(uint32_t) * 2)
			return;
		duel_mode->UpdateDeck(dp, pdata, len - 1);
		break;
	}
	case CTOS_HAND_RESULT: {
		if(!dp->game)
			return;
		if (len < 1 + sizeof(CTOS_HandResult))
			return;
		CTOS_HandResult packet;
		std::memcpy(&packet, pdata, sizeof packet);
		const auto* pkt = &packet;
		dp->game->HandResult(dp, pkt->res);
		break;
	}
	case CTOS_TP_RESULT: {
		if(!dp->game)
			return;
		if (len < 1 + sizeof(CTOS_TPResult))
			return;
		CTOS_TPResult packet;
		std::memcpy(&packet, pdata, sizeof packet);
		const auto* pkt = &packet;
		dp->game->TPResult(dp, pkt->res);
		break;
	}
	case CTOS_PLAYER_INFO: {
		if (len < 1 + sizeof(CTOS_PlayerInfo))
			return;
		CTOS_PlayerInfo packet;
		std::memcpy(&packet, pdata, sizeof packet);
		auto pkt = &packet;
		BufferIO::NullTerminate(pkt->name);
		BufferIO::CopyCharArray(pkt->name, dp->name);
		break;
	}
	case CTOS_EXTERNAL_ADDRESS: {
		// for other server & reverse proxy use only
		/*
		wchar_t hostname[LEN_HOSTNAME];
		uint32_t real_ip = ntohl(BufferIO::Read<int32_t>(pdata));
		BufferIO::CopyCharArray((uint16_t*)pdata, hostname);
		*/
		break;
	}
	case CTOS_CREATE_GAME: {
        std::optional<undo::Envelope> challenge;
        if(room_admission) {
            try { challenge=room_admission->Challenge(dp->undoPeer); }
            catch(...) { RejectUndoPeer(dp);return; }
        }
		if(dp->game || duel_mode)
			return;
		if (len < 1 + sizeof(CTOS_CreateGame))
			return;
		CTOS_CreateGame packet;
		std::memcpy(&packet, pdata, sizeof packet);
		auto pkt = &packet;
        if(room_admission && pkt->info.mode==MODE_TAG) { RejectUndoPeer(dp);return; }
		if(pkt->info.rule > CURRENT_RULE)
			pkt->info.rule = CURRENT_RULE;
		if(pkt->info.mode > MODE_TAG)
			pkt->info.mode = MODE_SINGLE;
		bool found = false;
		for (const auto& lflist : deckManager._lfList) {
			if(pkt->info.lflist == lflist.hash) {
				found = true;
				break;
			}
		}
		if (!found) {
			if (deckManager._lfList.size())
				pkt->info.lflist = deckManager._lfList[0].hash;
			else
				pkt->info.lflist = 0;
		}
		if (pkt->info.mode == MODE_SINGLE) {
			duel_mode = new SingleDuel(false);
		}
		else if (pkt->info.mode == MODE_MATCH) {
			duel_mode = new SingleDuel(true);
		}
		else if (pkt->info.mode == MODE_TAG) {
			duel_mode = new TagDuel();
		}
		else
			return;
		duel_etimer = event_new(net_evbase, -1, EV_PERSIST, DuelTimer, duel_mode);
		duel_mode->host_info = pkt->info;
		BufferIO::NullTerminate(pkt->name);
		BufferIO::NullTerminate(pkt->pass);
		BufferIO::CopyCharArray(pkt->name, duel_mode->name);
		BufferIO::CopyCharArray(pkt->pass, duel_mode->pass);
		duel_mode->JoinGame(dp, 0, true);
        if(challenge) SendUndoToPlayer(dp,*challenge);
		StartBroadcast();
		break;
	}
	case CTOS_JOIN_GAME: {
		if (!duel_mode)
			return;
		if (len < 1 + sizeof(CTOS_JoinGame))
			return;
		        std::optional<undo::Envelope> challenge;
        if(room_admission) {
            size_t occupied=0;
            for(const auto& peer:users) if(peer.second.game==duel_mode && peer.second.type<2) ++occupied;
            if(occupied>=2) { RejectUndoPeer(dp);return; }
            try { challenge=room_admission->Challenge(dp->undoPeer); }
            catch(...) { RejectUndoPeer(dp);return; }
        }
        auto* joinedBev=dp->bev;
        duel_mode->JoinGame(dp, pdata, false);
        const auto joined=users.find(joinedBev);
        if(challenge && joined!=users.end() && joined->second.game==duel_mode)
            SendUndoToPlayer(&joined->second,*challenge);
		break;
	}
	case CTOS_LEAVE_GAME: {
		if (!duel_mode)
			return;
		duel_mode->LeaveGame(dp);
		break;
	}
	case CTOS_SURRENDER: {
		if (!duel_mode)
			return;
		duel_mode->Surrender(dp);
		break;
	}
	case CTOS_HS_TODUELIST: {
		if (!duel_mode || duel_mode->HasActiveDuel())
			return;
		duel_mode->ToDuelist(dp);
		break;
	}
	case CTOS_HS_TOOBSERVER: {
		if (!duel_mode || duel_mode->HasActiveDuel())
			return;
		duel_mode->ToObserver(dp);
		break;
	}
	case CTOS_HS_READY:
	case CTOS_HS_NOTREADY: {
		if (!duel_mode || duel_mode->HasActiveDuel())
			return;
		duel_mode->PlayerReady(dp, (CTOS_HS_NOTREADY - pktType) != 0);
		break;
	}
	case CTOS_HS_KICK: {
		if (!duel_mode || duel_mode->HasActiveDuel())
			return;
		if (len < 1 + sizeof(CTOS_Kick))
			return;
		CTOS_Kick packet;
		std::memcpy(&packet, pdata, sizeof packet);
		const auto* pkt = &packet;
		duel_mode->PlayerKick(dp, pkt->pos);
		break;
	}
	case CTOS_HS_START: {
		if (!duel_mode || duel_mode->HasActiveDuel())
			return;
		duel_mode->StartDuel(dp);
		break;
	}
	}
}
size_t NetServer::CreateChatPacket(unsigned char* src, int src_size, unsigned char* dst, uint16_t dst_player_type) {
	uint16_t src_msg[LEN_CHAT_MSG];
	std::memcpy(src_msg, src, src_size);
	const int src_len = src_size / sizeof(uint16_t);
	if (src_msg[src_len - 1] != 0)
		return 0;
	// STOC_Chat packet
	auto pdst = dst;
	BufferIO::Write<uint16_t>(pdst, dst_player_type);
	std::memcpy(pdst, src_msg, src_size);
	pdst += src_size;
	return sizeof(dst_player_type) + src_size;
}

}
