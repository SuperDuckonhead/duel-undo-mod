#pragma once
#include "duel_history.h"
#include "../../ocgcore/card_data.h"
#include <map>
#include <memory>
namespace irr { namespace io { class IFileSystem; } }
namespace ygo { class DataManager; }
namespace undo {
Digest Sha256(const Bytes& bytes);
// Practice keeps the historical full snapshot, including scenario dependencies.
// Duel never enumerates single/: unrelated practice files cannot block a room.
enum class ResourceScope { Duel, Practice };
// Immutable resolved executable resources. No read path ever falls back to disk.
class ResourceView {
public:
 static std::shared_ptr<const ResourceView> Capture(const std::string& runtimeRoot,
   ResourceScope scope = ResourceScope::Practice);
 static std::shared_ptr<const ResourceView> Capture(const std::string& runtimeRoot,
   const ygo::DataManager& data, bool preferExpansionScript,
   ResourceScope scope = ResourceScope::Practice);
 const Bytes& Read(const std::string& logicalPath) const;
 const card_data& Card(std::uint32_t code) const;
 Digest Fingerprint() const { return digest_; }
 const std::map<std::uint32_t, card_data>& Cards() const { return cards_; }
 const std::vector<std::string>& Priority() const { return priority_; }
 std::size_t ScriptCount() const { return contents_.size(); }
 // Shared with DataManager's live script reader; nullopt means not resolved.
 static std::optional<Bytes> Resolve(const std::string& root, irr::io::IFileSystem*, bool prefer,
   const std::string& logicalPath);
private:
 std::map<std::string,Bytes> contents_;
 std::map<std::uint32_t,card_data> cards_;
 std::vector<std::string> priority_;
 // Diagnostics only: never enters canonical bytes or the resource fingerprint.
 std::string source_root_;
 Digest digest_{};
};
}
