// A real framed TCP relay observes the actual DuelClient queue and native host.
// It only delays/duplicates lobby prerequisites and injects a stale result or a
// corrupt card ID; all capability handshakes and accepted replies are real.
class TestDeckRelay {
 SOCKET listener{INVALID_SOCKET},client{INVALID_SOCKET},host{INVALID_SOCKET};
 std::thread worker;
 std::mutex mutex;
 std::vector<Bytes> uploads;
 unsigned readies{};
 std::string failure;
 bool corrupt;
 static void sendAll(SOCKET socket,const Bytes& packet) {
  Bytes bytes{uint8_t(packet.size()),uint8_t(packet.size()>>8)};bytes.insert(bytes.end(),packet.begin(),packet.end());
  size_t sent{};while(sent<bytes.size()){int n=send(socket,reinterpret_cast<const char*>(bytes.data()+sent),int(bytes.size()-sent),0);CHECK(n>0);sent+=n;}
 }
 static bool receiveAll(SOCKET socket,unsigned char* data,size_t size) {
  while(size){int n=recv(socket,reinterpret_cast<char*>(data),int(size),0);if(n<=0)return false;data+=n;size-=n;}return true;
 }
 static Bytes receive(SOCKET socket) {
  unsigned char size[2];if(!receiveAll(socket,size,2))return {};
  Bytes result(unsigned(size[0])|(unsigned(size[1])<<8));if(!receiveAll(socket,result.data(),result.size()))return {};return result;
 }
 void run(unsigned short target) {
  try {
   client=accept(listener,nullptr,nullptr);CHECK(client!=INVALID_SOCKET);
   host=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(host!=INVALID_SOCKET);
   sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(0x7f000001);address.sin_port=htons(target);
   CHECK(connect(host,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
   Bytes joined,seat;unsigned helloCount{};
   for(;;) {
    fd_set set;FD_ZERO(&set);FD_SET(client,&set);FD_SET(host,&set);timeval timeout{10,0};
    CHECK(select(0,&set,nullptr,nullptr,&timeout)>0);
    for(bool fromClient:{true,false}) {
     auto source=fromClient?client:host,destination=fromClient?host:client;
     if(!FD_ISSET(source,&set))continue;
     auto packet=receive(source);if(packet.empty())return;
     if(fromClient) {
      if(packet[0]==TestDeckUploadOpcode) {
       {std::lock_guard<std::mutex> lock(mutex);uploads.push_back(packet);}
       // Before the real result: same connection but wrong generation/session.
       auto stale=Bytes(packet.begin()+1,packet.begin()+25);stale.push_back(0);stale[0]--;
       stale.insert(stale.begin(),TestDeckResultOpcode);sendAll(client,stale);
       stale[1]++;stale[9]^=1;sendAll(client,stale);
       if(corrupt)std::fill(packet.end()-4,packet.end(),0);
      }
      if(packet[0]==CTOS_HS_READY){std::lock_guard<std::mutex> lock(mutex);++readies;}
      sendAll(destination,packet);
     } else {
      if(packet[0]==STOC_JOIN_GAME){joined=packet;continue;}
      if(packet[0]==STOC_TYPE_CHANGE){seat=packet;continue;}
      sendAll(destination,packet);
      if(packet[0]==RoomOuterOpcode && Decode(Bytes(packet.begin()+1,packet.end())).kind==WireKind::Hello && ++helloCount==2) {
       CHECK(!joined.empty()&&!seat.empty());
       // Capability arrives first, then duplicate host-seat, then join twice.
       sendAll(client,seat);sendAll(client,seat);sendAll(client,joined);sendAll(client,joined);
      }
      if(packet[0]==TestDeckResultOpcode)sendAll(destination,packet);
     }
    }
   }
  }catch(const std::exception& error){std::lock_guard<std::mutex> lock(mutex);failure=error.what();}
 }
public:
 unsigned short port{};
 TestDeckRelay(unsigned short target,bool inject):corrupt(inject) {
  listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(listener!=INVALID_SOCKET);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(0x7f000001);
  CHECK(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);CHECK(listen(listener,1)==0);
  int size=sizeof(address);CHECK(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0);port=ntohs(address.sin_port);
  worker=std::thread([this,target]{run(target);});
 }
 ~TestDeckRelay(){if(client!=INVALID_SOCKET)shutdown(client,SD_BOTH);if(host!=INVALID_SOCKET)shutdown(host,SD_BOTH);closesocket(listener);if(worker.joinable())worker.join();if(client!=INVALID_SOCKET)closesocket(client);if(host!=INVALID_SOCKET)closesocket(host);}
 void Check(const TestDuelConfig& expected,const SessionId& session,bool success) {
  std::lock_guard<std::mutex> lock(mutex);if(!failure.empty())throw std::runtime_error(failure);
  CHECK(uploads.size()==1);CHECK(readies==(success?1u:0u));
  auto bytes=uploads.front();CHECK(bytes.size()==33+4*(expected.main.size()+expected.extra.size()));
  CHECK(MatchesTestDeck(Bytes(bytes.begin()+1,bytes.end()),expected.generation,session));
  CHECK(bytes[25]==expected.main.size() && bytes[29]==expected.extra.size());
  for(size_t i=0;i<expected.main.size();++i)CHECK(TestDeckRead(bytes,33+4*i,4)==expected.main[i]);
  for(size_t i=0;i<expected.extra.size();++i)CHECK(TestDeckRead(bytes,33+4*(expected.main.size()+i),4)==expected.extra[i]);
 }
};
static int deckTestUploadGame() {
 WSADATA winsock{};CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);CHECK(evthread_use_windows_threads()==0);
 game.wMainMenu->setVisible(false);game.wDeckEdit->setVisible(true);game.is_building=true;
 game.cbDBCategory->clear();for(auto name:{L"pack",L"bot",L"deck"})game.cbDBCategory->addItem(name);
 game.cbDBCategory->setSelected(2);game.cbDBDecks->clear();game.ebDeckname->setText(L"unsaved test name");
 game.deckBuilder.Initialize();game.device->setEventReceiver(&game.deckBuilder);
 // Same native category setup as the established editor harness. No file
 // selection is needed for a new editable construction.
 game.env->setFocus(nullptr);
 CHECK(!game.deckBuilder.readonly && !game.deckBuilder.showing_pack && game.cbDBDecks->getSelected()==-1);
 CHECK(!game.is_siding && !game.deckBuilder.HasEditorSuspension());
 const auto fileBytes=[](const char* path){std::ifstream file(path,std::ios::binary);if(!file.good())throw std::runtime_error("Cannot read fixture "+std::filesystem::absolute(path).u8string());return std::string(std::istreambuf_iterator<char>(file),{});};
 auto& cards=dataManager.GetDataTable();auto& editor=game.deckBuilder;
 const auto A=&cards.at(89631139),B=&cards.at(46986414),X=&cards.at(23995346);
 CHECK(!(A->type&TYPES_EXTRA_DECK) && (X->type&TYPES_EXTRA_DECK));
 deckManager.current_deck.main={A,B,A};deckManager.current_deck.extra={X,X};deckManager.current_deck.side={B,A};
 editor.ResetEditorHistory();CHECK(deckManager.SaveDeck(deckManager.current_deck,L"./deck/deck-test-source.ydk"));editor.EditorDeckSaved();
 const auto original=fileBytes("deck/deck-test-source.ydk"),conf=fileBytes("system.conf");
 Deck other;other.main={B,B,B,B,B};CHECK(deckManager.SaveDeck(other,L"./deck/deck-test-other.ydk"));
 const auto otherBytes=fileBytes("deck/deck-test-other.ydk");
 editor.BeginEditorEdit();CHECK(editor.editorEditStart.has_value());
 std::swap(deckManager.current_deck.main[0],deckManager.current_deck.main[1]);
 deckManager.current_deck.main.push_back(A);editor.FinishEditorEdit(true);CHECK(editor.editorHistory.CanUndo());
 const auto expected=editor.CaptureEditorDeck();CHECK(editor.is_modified);
 game.cbCategorySelect->clear();game.cbCategorySelect->addItem(L"Fixture category");game.cbCategorySelect->setSelected(0);
 game.cbDeckSelect->clear();game.cbDeckSelect->addItem(L"deck-test-other");game.cbDeckSelect->setSelected(0);
 BufferIO::CopyWideString(L"saved category",game.gameConf.lastcategory);BufferIO::CopyWideString(L"saved deck",game.gameConf.lastdeck);
 game.cbBotRule->setSelected(2);game.ebNickName->setText(L"Frozen player");
 const auto category=std::wstring(game.gameConf.lastcategory),selected=std::wstring(game.gameConf.lastdeck);
 auto base=CaptureRoomConfig(dataManager,std::filesystem::current_path().u8string(),false,RoomMode::LoopbackFree);
 for(bool corrupt:{false,true,false}) {
  irr::SEvent click{};click.EventType=irr::EET_GUI_EVENT;click.GUIEvent.Caller=game.btnTestDeck;click.GUIEvent.EventType=irr::gui::EGET_BUTTON_CLICKED;
  editor.OnEvent(click);CHECK(editor.HasDeckTestPreparation());auto frozen=editor.DeckTestConfig();CHECK(frozen);
  CHECK(frozen->main==expected[0] && frozen->extra==expected[1]);CHECK(frozen->host.duel_rule==5);
  auto config=std::make_shared<RoomConfig>(*base);config->deckTest=frozen;
  unsigned short nativePort{};CHECK(NetServer::StartServer(0,0x7f000001,&nativePort,false,&config->capability,config));
  {
   TestDeckRelay relay(nativePort,corrupt);
   // Asynchronous connection handlers must not recapture any of these values.
   deckManager.current_deck=other;game.cbBotRule->setSelected(0);game.ebNickName->setText(L"Changed later");
   auto upload=DuelClient::StartDeckTestClient(0x7f000001,relay.port,config);CHECK(upload);
   CHECK(!DuelClient::StartDeckTestClient(0x7f000001,relay.port,config));
   // Ordinary menu ready callback must not reload a deck or rewrite preferences.
   click.GUIEvent.Caller=game.btnHostPrepReady;game.menuHandler.OnEvent(click);
   const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(12);
   while(std::chrono::steady_clock::now()<until) {
    game.device->run();{std::lock_guard<std::mutex> lock(game.gMutex);game.DrawGUI();}
    const auto state=upload->Inspect();if(corrupt?state.error!=TestDeckError::None:state.humanReady)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }
   auto state=upload->Inspect();CHECK(state.joined && state.hostSeat && state.capability && state.uploaded);
   if(corrupt){CHECK(state.error==TestDeckError::UnknownCard);CHECK(!state.readySent && !state.humanReady);}
   else {CHECK(state.error==TestDeckError::None);CHECK(state.readySent && state.humanReady);}
   relay.Check(*frozen,state.session,!corrupt);
   CHECK(!game.wHostPrepare->isVisible());CHECK(game.stTestDeckStatus->isVisible());
   CHECK(game.dInfo.duel_rule==5 && game.dInfo.start_lp==8000 && game.dInfo.time_limit==0);
   CHECK(std::wstring(game.cbCategorySelect->getText())==L"Fixture category");
   CHECK(std::wstring(game.cbDeckSelect->getText())==L"deck-test-other");
   CHECK(std::wstring(game.gameConf.lastcategory)==category && std::wstring(game.gameConf.lastdeck)==selected);
   CHECK(fileBytes("deck/deck-test-source.ydk")==original && fileBytes("deck/deck-test-other.ydk")==otherBytes && fileBytes("system.conf")==conf);
   DuelClient::StopClient();NetServer::StopServer();
   for(int n=0;n<2000 && (!upload->Inspect().closed || NetServer::IsRunning());++n)std::this_thread::sleep_for(std::chrono::milliseconds(2));
   CHECK(upload->Inspect().closed && !NetServer::IsRunning());
  }
  CHECK(editor.ResumeDeckTestPreparation());CHECK(editor.CaptureEditorDeck()==expected);CHECK(editor.is_modified);
  CHECK(editor.editorHistory.CanUndo());game.cbBotRule->setSelected(2);game.ebNickName->setText(L"Frozen player");
  std::cout<<"PASS actual Game/TCP upload: "<<(corrupt?"unknown-ID error before ready":"exact memory + native host accepted/auto-ready once")<<std::endl;
 }
 CHECK(editor.UndoEditorEdit());CHECK(editor.CaptureEditorDeck()[0]==std::vector<uint32_t>({89631139,46986414,89631139}));CHECK(!editor.is_modified);
 std::cout<<"PASS actual ordered unsaved memory, side-only snapshot, selector/preferences/file isolation, editor undo and retry\n";
 return 0;
}
