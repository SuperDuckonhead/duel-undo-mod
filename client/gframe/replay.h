#ifndef REPLAY_H
#define REPLAY_H

#include <cstdio>
#include <vector>
#include <string>
#include "deck.h"
#include "undo/core_driver.h"
#include "../ocgcore/common.h"

namespace ygo {

// replay flag
#define REPLAY_COMPRESSED	0x1
#define REPLAY_TAG			0x2
#define REPLAY_DECODED		0x4
#define REPLAY_SINGLE_MODE	0x8
#define REPLAY_UNIFORM		0x10

#define REPLAY_UNDO_CORE 0x20

#define REPLAY_ID_YRP1	0x31707279
#define REPLAY_ID_YRP2	0x32707279

struct ReplayHeader {
	uint32_t id{};
	uint32_t version{};
	uint32_t flag{};
	uint32_t seed{};
	uint32_t datasize{};
	uint32_t start_time{};
	uint8_t props[8]{};
};

struct ExtendedReplayHeader {
	ReplayHeader base;
	uint32_t seed_sequence[SEED_COUNT]{};
	uint32_t header_version{ 1 };
	uint32_t value1{};
	uint32_t value2{};
	uint32_t value3{};
};

struct DuelParameters {
	int32_t start_lp{};
	int32_t start_hand{};
	int32_t draw_count{};
	uint32_t duel_flag{};
};

// max size
constexpr size_t MAX_REPLAY_SIZE = 0x80000;
constexpr size_t MAX_COMP_SIZE = UINT16_MAX - 1 - sizeof(ExtendedReplayHeader); // UINT16_MAX - 1 : MAX_DATA_SIZE

class Replay {
public:
	Replay();
	~Replay();

	// record
	void BeginRecord();
    void RecordUndoSingle(const undo::InitialState&, const std::vector<undo::ResponseRecord>&, const wchar_t* hostName, const wchar_t* clientName);
    void RecordUndoDuel(const undo::InitialState&, const std::vector<undo::ResponseRecord>&, const wchar_t* hostName, const wchar_t* clientName);
    // Complete header/body, memory only. Invalid Load preserves record and cursor.
    undo::Bytes ExportUndoReplay() const;
    bool LoadUndoReplay(const undo::Bytes&);
    const undo::InitialState& UndoInitial() const;
    std::shared_ptr<const undo::ResourceView> CaptureUndoResources(const std::string& root, const DataManager&, bool preferExpansionScript) const;
    std::unique_ptr<undo::CoreDriver> CreateUndoDriver(std::shared_ptr<const undo::ResourceView>) const;
    bool ReadUndoResponse(const undo::Checkpoint&, undo::Bytes&);
	void WriteHeader(ExtendedReplayHeader& header);
	void WriteData(const void* data, size_t length, bool flush = true);
	template<typename T>
	void Write(T data, bool flush = true) {
		WriteData(&data, sizeof(T), flush);
	}
	void WriteInt32(int32_t data, bool flush = true);
	size_t WriteResponse(const void* data, size_t length);
	bool RemoveData(size_t length);
	void Flush();
	void EndRecord();
	bool SaveReplay(const wchar_t* base_name);

	// play
	static bool DeleteReplay(const wchar_t* name);
	static bool RenameReplay(const wchar_t* oldname, const wchar_t* newname);
	static size_t GetDeckPlayer(size_t deck_index) {
		switch (deck_index) {
		case 2:
			return 3;
		case 3:
			return 2;
		default:
			return deck_index;
		}
	}
	bool OpenReplay(const wchar_t* name);
	bool ReadNextResponse(unsigned char resp[]);
	bool ReadName(wchar_t* data);
	void ReadHeader(ExtendedReplayHeader& header);
	bool ReadData(void* data, size_t length);
	template<typename T>
	T Read() {
		T ret{};
		ReadData(&ret, sizeof(T));
		return ret;
	}
	int32_t ReadInt32();
	void Rewind();
	void Reset();
	void SkipInfo();
	bool IsReplaying() const;
	bool SaveDeck(size_t index, const wchar_t* filename);

	FILE* fp{ nullptr };
	ExtendedReplayHeader pheader;
	unsigned char* comp_data;
	size_t comp_size{};

	std::vector<std::wstring> players;	// 80 or 160 bytes
	DuelParameters params;				// 16 bytes

	std::vector<DeckArray> decks;		// 4 bytes, main deck, 4 bytes, extra deck
	std::string script_name;			// 2 bytes, script name (max: 256 bytes)

private:
	bool ReadInfo();
    bool ReadUndoInfo();
    void RecordUndo(const undo::InitialState&, const std::vector<undo::ResponseRecord>&, const wchar_t*, const wchar_t*, bool single);
    void SwapUndoRecord(Replay&) noexcept;
    struct UndoResponse { std::uint8_t player{}; undo::Origin origin{}; undo::Bytes response; undo::Digest promptDigest{}, transcriptDigest{}; };
    undo::InitialState undo_initial_;
    std::vector<UndoResponse> undo_responses_;
    std::size_t undo_response_index_{};

	unsigned char* replay_data;
	size_t replay_size{};
	size_t data_position{};
	size_t info_offset{};
	bool is_recording{};
	bool is_replaying{};
	bool can_read{};
};

}

#endif
