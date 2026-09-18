#include "room_config.h"
#include "../bufferio.h"
#include "../data_manager.h"
#include "../config.h"
#include "runtime_paths.h"
#include "deck_test_upload.h"
#include <cstring>
#include <fstream>
#include <IFileArchive.h>
#include <IFileSystem.h>
#include <irrXML.h>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <windows.h>
#include <shellapi.h>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
namespace undo {
namespace {
Bytes read(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot capture room input: " + path.u8string());
  return Bytes(std::istreambuf_iterator<char>(file), {});
}
void put(Bytes &b, uint64_t n) {
  for (unsigned i = 0; i < 8; ++i)
    b.push_back(uint8_t(n >> (i * 8)));
}
Digest profile(const char* name, std::uint16_t version, std::uint16_t nativeVersion = 0) {
  Bytes bytes(name, name + std::strlen(name));
  put(bytes, version);
  put(bytes, nativeVersion);
  return Sha256(bytes);
}
Hello capability(const ygo::DataManager& data, RoomMode mode) {
  Hello result;
  result.mode = mode;
  // These identities describe serialized formats, never local program bytes.
  result.engine = profile("ygopro-native-game-messages", GameMessageProfileVersion, PRO_VERSION);
  result.rules = profile("ygopro-undo-player-restore-status", RestoreProfileVersion);
  // Hash the resolved numeric rows used by the native selection UI. Do not
  // serialize struct padding, map order, translated text, database files,
  // restriction lists or effect scripts. rule_code and extra setcodes matter
  // for declarable-card predicates even when the backing SQL schema omits them.
  const std::string domain = "ygopro-native-selection-cards-v1";
  Bytes cards(domain.begin(), domain.end());
  std::map<std::uint32_t, const ygo::CardDataC*> ordered;
  for (const auto& entry : data.GetDataTable()) ordered.emplace(entry.first, &entry.second);
  put(cards, ordered.size());
  for (const auto& entry : ordered) {
    const auto& c = *entry.second;
    put(cards, entry.first);
    put(cards, c.code); put(cards, c.alias);
    for (auto value : c.setcode) put(cards, value);
    for (auto value : {c.type, c.level, c.attribute, c.race,
         static_cast<std::uint32_t>(c.attack), static_cast<std::uint32_t>(c.defense),
         c.lscale, c.rscale, c.link_marker, c.rule_code, c.ot, c.category}) put(cards, value);
  }
  result.resources = Sha256(cards);
  return result;
}
struct XmlBytes final : irr::io::IFileReadCallBack {
  Bytes bytes;
  size_t cursor{};
  explicit XmlBytes(Bytes b) : bytes(std::move(b)) {}
  int read(void *out, int n) override {
    auto count = std::min(bytes.size() - cursor, size_t(std::max(0, n)));
    std::memcpy(out, bytes.data() + cursor, count);
    cursor += count;
    return int(count);
  }
  long getSize() const override { return long(bytes.size()); }
};
std::string escape(const std::string &s) {
  std::string out;
  for (auto c : s)
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
    }
  return out;
}
void appSettings(const std::filesystem::path &path,
                 std::map<std::string, std::string> &values,
                 std::vector<std::string> &provenance, unsigned depth = 0) {
  if (depth > 4)
    throw std::runtime_error("Cyclic appSettings source");
  auto source = read(path);
  auto hash = Sha256(source);
  std::string fingerprint;
  const char *hex = "0123456789abcdef";
  for (auto byte : hash) {
    fingerprint += hex[byte >> 4];
    fingerprint += hex[byte & 15];
  }
  provenance.push_back(path.u8string() + "#" + fingerprint);
  XmlBytes input(std::move(source));
  std::unique_ptr<irr::io::IrrXMLReader> xml(
      irr::io::createIrrXMLReader(&input, false));
  if (!xml)
    throw std::runtime_error("Cannot parse appSettings");
  bool inside = false;
  std::string external;
  while (xml->read()) {
    std::string node = xml->getNodeName();
    if (xml->getNodeType() == irr::io::EXN_ELEMENT_END &&
        node == "appSettings") {
      inside = false;
      continue;
    }
    if (xml->getNodeType() != irr::io::EXN_ELEMENT)
      continue;
    auto attr = [&](const char *n) {
      const char *v = xml->getAttributeValue(n);
      return std::string(v ? v : "");
    };
    if (node == "appSettings") {
      inside = true;
      auto source = attr("configSource");
      external = attr("file");
      if (!source.empty()) {
        appSettings(path.parent_path() / std::filesystem::u8path(source),
                    values, provenance, depth + 1);
        inside = false;
      }
    } else if (inside) {
      if (node == "clear")
        values.clear();
      else if (node == "remove")
        values.erase(attr("key"));
      else if (node == "add") {
        auto key = attr("key");
        if (key.empty())
          throw std::runtime_error("Empty appSettings key");
        values[key] = attr("value");
      } else
        throw std::runtime_error("Unsupported appSettings element");
    }
  }
  if (!external.empty()) {
    auto extra = path.parent_path() / std::filesystem::u8path(external);
    if (std::filesystem::exists(extra))
      appSettings(extra, values, provenance, depth + 1);
  }
}
} // namespace
std::string ResolveDeckTestBotSelection(const Bytes& catalog) {
  std::istringstream input(std::string(catalog.begin(),catalog.end()));
  std::string line,selected;
  const auto trim=[](const std::wstring& text) {
    const auto first=text.find_first_not_of(L" \t\r\n\v\f");
    return first==std::wstring::npos?std::wstring{}:text.substr(first,text.find_last_not_of(L" \t\r\n\v\f")-first+1);
  };
  while(std::getline(input,line)) {
    const auto first=line.find_first_not_of(" \t\r\n");
    if(first==std::string::npos || line[first]!='!')continue;
    std::string command,description,flags;
    if(!std::getline(input,command) || !std::getline(input,description) || !std::getline(input,flags))
      throw std::invalid_argument("Truncated bot.conf entry for deck test");
    if(!command.empty() && command.back()=='\r')command.pop_back();
    auto wide=L"WindBot "+BufferIO::DecodeUTF8String(command);
    std::replace(wide.begin(),wide.end(),L'\'',L'"');
    int count{};auto argv=CommandLineToArgvW(wide.c_str(),&count);
    if(!argv)throw std::runtime_error("Cannot parse deck test bot selection");
    struct FreeArgs {LPWSTR* value;~FreeArgs(){LocalFree(value);}} guard{argv};
    struct IgnoreCase {bool operator()(const std::wstring& a,const std::wstring& b) const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
    std::set<std::wstring,IgnoreCase> keys;
    bool matches=false;
    for(int i=1;i<count;++i) {
      const std::wstring argument=argv[i];const auto equals=argument.find(L'=');
      if(equals==std::wstring::npos)throw std::invalid_argument("Invalid bot.conf command for deck test");
      const auto key=trim(argument.substr(0,equals)),value=trim(argument.substr(equals+1));
      if(key.empty() || !keys.insert(key).second)throw std::invalid_argument("Duplicate bot.conf command key for deck test");
      if(_wcsicmp(key.c_str(),L"Deck")==0 && value==L"MokeyMokeyKing")matches=true;
    }
    if(matches) {
      if(!selected.empty())throw std::invalid_argument("Multiple bot.conf entries select Deck=MokeyMokeyKing");
      selected=std::move(command);
    }
  }
  if(selected.empty())throw std::invalid_argument("Missing bot.conf entry with Deck=MokeyMokeyKing");
  return selected;
}
std::shared_ptr<const RoomConfig> CaptureDeckTestRoomConfig(ygo::DataManager& data,
    const std::string& runtimeRoot,bool prefer,std::shared_ptr<const TestDuelConfig> test) {
  if(!test)throw std::invalid_argument("Missing frozen deck test snapshot");
  const auto& host=test->host;
  if(host.rule!=5 || host.mode!=MODE_SINGLE || host.start_lp!=8000 || host.start_hand!=5 ||
     host.draw_count!=1 || host.time_limit || host.lflist || !host.no_check_deck || !host.no_shuffle_deck)
    throw std::invalid_argument("Invalid fixed deck test room settings");
  const auto catalog=read(std::filesystem::u8path(runtimeRoot)/"bot.conf");
  const auto selection=ResolveDeckTestBotSelection(catalog);
  auto config=std::make_shared<RoomConfig>(*CaptureRoomConfig(data,runtimeRoot,prefer,RoomMode::LoopbackFree,selection));
  if(config->bot->selectionCatalog!=catalog)throw std::runtime_error("bot.conf changed while capturing deck test");
  config->bot->handOverride=0;
  config->deckTest=std::move(test);
  return config;
}
std::future<std::shared_ptr<const RoomConfig>> PrepareDeckTestRoomConfig(ygo::DataManager& data,
    const std::string& runtimeRoot,bool prefer,std::shared_ptr<const TestDuelConfig> test) {
  struct ArchiveSource {
    std::string path,password;
    irr::io::E_FILE_ARCHIVE_TYPE type;
  };
  std::vector<ArchiveSource> archives;
  if(auto* files=data.IrrFileSystem) {
    archives.reserve(files->getFileArchiveCount());
    for(irr::u32 i=0;i<files->getFileArchiveCount();++i) {
      const auto* archive=files->getFileArchive(i);
      archives.push_back({std::filesystem::absolute(std::filesystem::u8path(archive->getArchiveName().c_str())).u8string(),
        archive->Password.c_str(),archive->getType()});
    }
  }
  // DataManager owns its card/string/extra-setcode containers. Its filesystem
  // and fallback text are borrowed pointers, so replace both in the worker.
  auto loaded=data;
  loaded.IrrFileSystem=nullptr;
  const std::wstring unknown=data.unknown_string?data.unknown_string:L"";
  loaded.unknown_string=nullptr;
  const auto root=std::filesystem::absolute(std::filesystem::u8path(runtimeRoot)).u8string();
  return std::async(std::launch::async,[loaded=std::move(loaded),archives=std::move(archives),
      unknown,root,prefer,test=std::move(test)]() mutable {
    std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(
      irr::io::createFileSystem(),[](auto* p){if(p)p->drop();});
    if(!files)throw std::runtime_error("Cannot create deck test resource filesystem");
    for(const auto& archive:archives) {
      // Match Game::LoadExpansions: case-insensitive names with paths intact.
      if(!files->addFileArchive(archive.path.c_str(),true,false,archive.type,archive.password.c_str()))
        throw std::runtime_error("Cannot reopen deck test resource archive: "+archive.path);
    }
    loaded.IrrFileSystem=files.get();loaded.unknown_string=unknown.c_str();
    return CaptureDeckTestRoomConfig(loaded,root,prefer,std::move(test));
  });
}
std::shared_ptr<const RoomConfig>
CaptureClientRoomConfig(ygo::DataManager& data, RoomMode mode) {
  auto config = std::make_shared<RoomConfig>();
  config->capability = capability(data, mode);
  return config;
}
std::shared_ptr<const RoomConfig>
CaptureRoomConfig(ygo::DataManager &data, const std::string &runtimeRoot,
                  bool prefer, RoomMode mode, const std::string &selection,
                  const std::string &custom) {
  auto config = std::make_shared<RoomConfig>();
  const auto root =
      std::filesystem::absolute(std::filesystem::u8path(runtimeRoot))
          .lexically_normal();
  config->resources = data.CaptureResources(root.u8string(), prefer, ResourceScope::Duel);
  config->capability = capability(data, mode);
  if (!selection.empty()) {
    BotLaunchData bot;
    bot.runtimeRoot = (root / "WindBot").u8string();
    bot.selectionCommand = selection;
    bot.selectionCatalog = read(root / "bot.conf");
    // Private AI restoration retains exact local executable/resource identity.
    // These are deliberately independent of the peer compatibility contract.
    std::vector<wchar_t> exe(32768);
    auto count = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
    if (!count || count >= exe.size())
      throw std::runtime_error("Cannot identify running engine");
    bot.engine = Sha256(read(std::filesystem::path(std::wstring(exe.data(), count))));
    bot.resources = config->resources->Fingerprint();
    bot.cardView = CaptureBotCardView(*config->resources, data, bot.engine);
    bot.seed = int32_t(std::random_device{}());
    config->botExecutable =
        (ExecutableRoot() / "WindBot" / "WindBot-undo.exe").wstring();
    if (!std::filesystem::is_regular_file(config->botExecutable))
      throw std::runtime_error(
          "Missing private WindBot executable beside client");
    for (const auto &label :
         DiscoverBotConfigSources(selection, bot.selectionCatalog)) {
      auto path = std::filesystem::u8path(label);
      if (!path.is_absolute())
        path = std::filesystem::u8path(bot.runtimeRoot) / path;
      auto content = read(path);
      bot.selectionConfigs.push_back({label, content, Sha256(content)});
    }
    auto app = root / "WindBot" / "WindBot.exe.config";
    if (!std::filesystem::exists(app))
      app = std::filesystem::path(config->botExecutable + L".config");
    if (std::filesystem::exists(app)) {
      std::map<std::string, std::string> values;
      std::vector<std::string> provenance;
      appSettings(app, values, provenance);
      std::string xml = "<configuration><appSettings>";
      for (auto &v : values)
        xml += "<add key=\"" + escape(v.first) + "\" value=\"" +
               escape(v.second) + "\"/>";
      xml += "</appSettings></configuration>";
      Bytes bytes(xml.begin(), xml.end());
      std::string label;
      for (auto &part : provenance) {
        if (!label.empty())
          label += ";";
        label += part;
      }
      bot.appSettings = BotFrozenConfig{label, bytes, Sha256(bytes)};
    }
    if (!custom.empty()) {
      auto path = std::filesystem::u8path(custom);
      if (!path.is_absolute())
        path = root / path;
      path = path.lexically_normal();
      bot.customDeckSource = path.u8string();
      bot.customDeck = read(path);
      bot.hasCustomDeck = true;
    }
    config->bot = std::move(bot);
  }
  return config;
}
} // namespace undo
