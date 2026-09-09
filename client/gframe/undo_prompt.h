#pragma once
#include <memory>
namespace ygo {
class Game;
class ClientField;
struct PromptSnapshot;
// Both operations run under gMutex. Snapshots contain pristine live presentation,
// never engine data or a live model pointer. Preparation touches hidden trees only.
std::shared_ptr<const PromptSnapshot> CaptureUndoPrompt(Game&);
class PreparedPrompt {
public:
    virtual ~PreparedPrompt() = default;
    virtual void Install() noexcept = 0;
};
std::unique_ptr<PreparedPrompt> PrepareUndoPrompt(Game&, const PromptSnapshot&, const ClientField* candidate = nullptr);
}
