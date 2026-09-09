#include "room_config.h"
#include "../bufferio.h"
#include "../data_manager.h"
#include "../deck_manager.h"
#include "runtime_paths.h"
#include <cstring>
#include <fstream>
#include <irrXML.h>
#include <map>
#include <random>
#include <windows.h>
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
void blob(Bytes &b, const Bytes &v) {
  put(b, v.size());
  b.insert(b.end(), v.begin(), v.end());
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
std::shared_ptr<const RoomConfig>
CaptureRoomConfig(ygo::DataManager &data, const std::string &runtimeRoot,
                  bool prefer, RoomMode mode, const std::string &selection,
                  const std::string &custom) {
  auto config = std::make_shared<RoomConfig>();
  const auto root =
      std::filesystem::absolute(std::filesystem::u8path(runtimeRoot))
          .lexically_normal();
  config->resources = data.CaptureResources(root.u8string(), prefer);
  std::vector<wchar_t> exe(32768);
  auto count = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
  if (!count || count >= exe.size())
    throw std::runtime_error("Cannot identify running engine");
  config->capability.engine =
      Sha256(read(std::filesystem::path(std::wstring(exe.data(), count))));
  config->capability.resources = config->resources->Fingerprint();
  config->capability.mode = mode;
  // Canonical logical values from the exact live lflists, independent of their
  // source path and map iteration order. The executable binds compiled rules.
  Bytes rules;
  put(rules, ygo::deckManager._lfList.size());
  for (const auto &list : ygo::deckManager._lfList) {
    put(rules, list.hash);
    put(rules, list.listName.size());
    for (auto ch : list.listName)
      put(rules, uint32_t(ch));
    std::map<uint32_t, int> ordered(list.content.begin(), list.content.end());
    put(rules, ordered.size());
    for (auto &entry : ordered) {
      put(rules, entry.first);
      put(rules, entry.second);
    }
  }
  config->capability.rules = Sha256(rules);
  if (!selection.empty()) {
    BotLaunchData bot;
    bot.runtimeRoot = (root / "WindBot").u8string();
    bot.selectionCommand = selection;
    bot.selectionCatalog = read(root / "bot.conf");
    bot.engine = config->capability.engine;
    bot.resources = config->capability.resources;
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
