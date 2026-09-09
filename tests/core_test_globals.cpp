// Standalone resource/engine tests never instantiate the GUI. The retained COFF
// reference to its pointer comes from the unused legacy DataManager callback.
namespace ygo { class Game; Game* mainGame = nullptr; }