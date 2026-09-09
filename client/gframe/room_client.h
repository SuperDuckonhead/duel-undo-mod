#pragma once
#include "undo/room_wire.h"
#include "undo/single_undo.h"
#include <functional>
#include <memory>
namespace ygo {
class Game;
// Receive/Poll/destruction belong to the client transport thread. UI entry
// points only publish immutable commands; no bufferevent call crosses threads.
class RoomClient {
public:
  using Send = std::function<void(const undo::Envelope &)>;
  using Apply = std::function<void(const undo::Bytes &)>;
  using Deferred = std::function<void(const undo::InputSubmission &)>;
  RoomClient(Game &, undo::SessionId, Send, Apply, Deferred);
  ~RoomClient();
  // Transport owner disposes Game-owned resources before releasing publication.
  void Close();
  void Receive(const undo::Envelope &);
  void Poll();
  bool Submit(const undo::InputSubmission &);
  bool QueueLegacy(const undo::Bytes &);
  bool RequestUndo();
  bool Consent(bool);
  undo::InputToken Token() const;
  bool InputPaused() const;
  bool PresentationFrozen() const;
  bool CanUndo() const;
  bool NeedsConsent() const;
  std::wstring StatusText() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace ygo
