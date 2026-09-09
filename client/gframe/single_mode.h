#ifndef SINGLE_MODE_H
#define SINGLE_MODE_H

#include <cstdint>
#include <vector>
#include "replay.h"
#include "undo/single_undo.h"
#include <atomic>

namespace ygo {

class SingleMode {
private:
	static std::atomic<bool> is_closing;
	static std::atomic<bool> is_continuing;
	static void ReloadLocation(int player, int location, int flag, std::vector<unsigned char>& queryBuffer);

public:
	static bool StartPlay();
	static void StopPlay(bool is_exiting = false);
	static bool SetResponse(const undo::InputSubmission&);
 static undo::InputToken CurrentToken();
 static bool RequestUndo(uint8_t player);
 static bool CanUndo(uint8_t player);
 static bool InputPaused();
 static std::string LastUndoError();
 static std::shared_ptr<undo::SingleUndo> ActiveSession();
	static void SinglePlayThread();
	static bool SinglePlayAnalyze(unsigned char* msg, unsigned int len);
	
	static void SinglePlayRefresh(int flag = 0xf81fff);
	static void SingleRefreshLocation(int player, int location, int flag);
	static void SinglePlayRefreshHand(int player, int flag = 0x781fff);
	static void SinglePlayRefreshGrave(int player, int flag = 0x181fff);
	static void SinglePlayRefreshDeck(int player, int flag = 0x181fff);
	static void SinglePlayRefreshExtra(int player, int flag = 0x181fff);
	static void SinglePlayRefreshSingle(int player, int location, int sequence, int flag = 0xf81fff);
	static void SinglePlayReload();


protected:
	static Replay last_replay;
};

}

#endif //SINGLE_MODE_H
