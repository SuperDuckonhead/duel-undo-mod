#include "game.h"
#include "data_manager.h"
#include "duelclient.h"
#include "netserver.h"
#include "undo/deck_test_upload.h"
#include "undo/room_config.h"
#include <chrono>
#include <future>
#include <thread>

namespace ygo {
struct DeckTestSession {
	std::future<std::shared_ptr<const undo::RoomConfig>> capture;
	std::shared_ptr<const undo::RoomConfig> room;
	std::shared_ptr<undo::DeckTestUpload> upload;
	std::chrono::steady_clock::time_point deadline;
	std::wstring failure;
	bool ownsServer{}, startSent{}, stopping{}, serverStopSent{};
};

void DeckBuilder::ExitDeckTest() {
	if(deckTestSession) deckTestSession->stopping=true;
}

void DeckBuilder::PollDeckTest() {
	if(!HasDeckTestPreparation()) return;
	if(!deckTestSession) {
		deckTestSession=std::make_shared<DeckTestSession>();
		auto& session=*deckTestSession;
		try {
			const auto config=DeckTestConfig();
			undo::EncodeTestDeck(*config,{}); // Reject transport capacity before acquiring anything.
			const auto root=mainGame->runtime_root.u8string();
			const bool prefer=mainGame->gameConf.prefer_expansion_script!=0;
			session.capture=undo::PrepareDeckTestRoomConfig(dataManager,root,prefer,config);
			mainGame->btnLeaveGame->setText(L"取消测试");
		} catch(const undo::TestDeckFailure& error) {
			session.failure=undo::TestDeckErrorText(error.error);session.stopping=true;
		} catch(const std::exception& error) {
			mainGame->ErrorLog(error.what());
			session.failure=L"无法准备测试资源："+BufferIO::DecodeUTF8String(error.what());session.stopping=true;
		}
	}
	const auto owner=deckTestSession;
	auto& session=*owner;
	if(session.capture.valid()) {
		if(session.capture.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
		try { session.room=session.capture.get(); }
		catch(const std::exception& error) {
			mainGame->ErrorLog(error.what());
			session.failure=L"无法启动卡组测试："+BufferIO::DecodeUTF8String(error.what());session.stopping=true;
		}
		if(!session.stopping) {
			unsigned short port{};
			session.ownsServer=NetServer::StartServer(0,0x7f000001,&port,false,&session.room->capability,session.room);
			if(!session.ownsServer) {
				session.failure=L"无法创建本机测试房间，请先结束已有房间";session.stopping=true;
			} else {
				session.upload=DuelClient::StartDeckTestClient(0x7f000001,port,session.room);
				if(!session.upload) {
					session.failure=L"无法连接本机测试房间，请先结束已有连接";session.stopping=true;
				}
				session.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
			}
		}
	}
	if(session.upload) {
		const auto state=session.upload->Inspect();
		if(state.error!=undo::TestDeckError::None && !session.stopping) {
			session.failure=undo::TestDeckErrorText(state.error);session.stopping=true;
		}
		if(state.finished || state.closed || state.cancelled) session.stopping=true;
		if(!session.stopping && !state.started && std::chrono::steady_clock::now()>session.deadline) {
			session.failure=L"悠悠王启动或连接超时，请检查机器人资源后重试";session.stopping=true;
		}
		if(!session.stopping && !session.startSent && state.humanReady && state.opponentReady) {
			std::lock_guard<std::mutex> lock(mainGame->gMutex);
			if(CommitDeckTestPreparation(&mainGame->menuHandler)) {
				mainGame->exit_on_return=false;
				mainGame->dInfo.isSingleMode=false;mainGame->dInfo.isReplay=false;
				mainGame->dInfo.isStarted=false;mainGame->dInfo.isInDuel=false;
				DuelClient::SendPacketToServer(CTOS_HS_START);session.startSent=true;
			} else {
				session.failure=L"无法切换到测试对局";session.stopping=true;
			}
		}
		if(session.stopping && !state.closed) {
			// Cancellation is consumed by this connection's event thread. Never
			// touch a libevent base concurrently with its teardown.
			session.upload->Cancel(session.upload->Config()->generation);
			// Repeat while closing: a callback may enter a wait after this frame.
			mainGame->frameSignal.Set();mainGame->actionSignal.Set();
			mainGame->replaySignal.Set();mainGame->closeDoneSignal.Set();
			return;
		}
	}
	if(!session.stopping) return;
	if(session.ownsServer) {
		if(!session.serverStopSent) {
			NetServer::StopDeckTestServer(session.room->deckTest);session.serverStopSent=true;
		}
		if(NetServer::IsRunning()) return;
	}
	std::lock_guard<std::mutex> lock(mainGame->gMutex);
	if(session.startSent) {
		mainGame->fadingList.clear();
		mainGame->CloseDuelWindow();mainGame->dField.Clear();
		mainGame->dInfo.isStarted=false;mainGame->dInfo.isInDuel=false;mainGame->dInfo.isFinished=false;
		mainGame->dInfo.isSingleMode=false;mainGame->dInfo.isReplay=false;
	}
	if(!ResumeDeckTestPreparation()) {
		mainGame->ErrorLog("Cannot restore suspended deck editor");
		return; // Keep the only complete snapshot available for recovery.
	}
	mainGame->ResizeChatInputWindow();
	deckTestSession.reset();
	if(!session.failure.empty()) mainGame->env->addMessageBox(L"卡组测试",session.failure.c_str());
}

void DeckBuilder::CloseDeckTestOnExit() {
	if(!deckTestSession) return;
	ExitDeckTest();
	while(deckTestSession) {
		PollDeckTest();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}
}
