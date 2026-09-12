/*
 * duel.h
 *
 *  Created on: 2010-4-8
 *      Author: Argon
 */

#ifndef DUEL_H_
#define DUEL_H_

#include "common.h"
#include "sort.h"
#include "mtrandom.h"
#include <set>
#include <unordered_set>
#include <vector>
#include <functional>

class card;
class group;
class effect;
class field;
class interpreter;
struct lua_State;
struct tevent;

using card_set = std::set<card*, card_sort>;

enum class native_query_api : uint8_t {
	ExistingMatching, MatchingGroup, SelectMatching, FusionMaterials,
	CheckFusion, FusionProcedure, SelectFusion, SelectedFusion
};

class duel {
public:
	char strbuffer[256]{};
	int32_t rng_version{ 2 };
 bool undo_deterministic{};
 uint64_t lua_seed[2]{};
	interpreter* lua;
	field* game_field;
	mtrandom random;
	// Host-private, per-duel semantic query bridge. Normal cores leave it absent.
	// The Lua state is borrowed only for this synchronous invocation.
	std::function<bool(lua_State*, bool, bool)> pool_query;
	// Candidate-only finite binding before filtering (null), then validation of
	// the original filtered Group. The callback never survives a Lua yield.
	std::function<void(lua_State*, group*)> pool_select;
	// Synchronous borrowed inputs, never a Lua suspension or a replacement Group.
	std::function<void(lua_State*, native_query_api, bool, const card_set*, card*, int32_t)> pool_observe;
	effect* pool_target_check{};
	const tevent* pool_target_event{};
	bool pool_selection_pending{};

	std::vector<byte> message_buffer;
	std::unordered_set<card*> cards;
	std::unordered_set<card*> assumes;
	std::unordered_set<group*> groups;
	std::unordered_set<group*> sgroups;
	std::unordered_set<effect*> effects;
	std::unordered_set<effect*> uncopy;
	
	explicit duel(const uint32_t* undo_seed = nullptr);
	~duel();
	void clear();
	
	uint32_t buffer_size() const {
		return (uint32_t)message_buffer.size() & PROCESSOR_BUFFER_LEN;
	}
	card* new_card(uint32_t code);
	group* new_group();
	group* new_group(card* pcard);
	group* new_group(const card_set& cset);
	effect* new_effect();
	void delete_card(card* pcard);
	void delete_group(group* pgroup);
	void delete_effect(effect* peffect);
	void release_script_group();
	void restore_assumes();
	int32_t read_buffer(byte* buf);
	void write_buffer(const void* data, size_t size);
	void write_buffer32(uint32_t value);
	void write_buffer16(uint16_t value);
	void write_buffer8(uint8_t value);
	void clear_buffer();
	void set_responsei(int32_t resp);
	void set_responseb(byte* resp);
	int32_t get_next_integer(int32_t l, int32_t h);
private:
	group* register_group(group* pgroup);
};

// Reentrant scope around actual native queries. End it before any lua_yieldk:
// Lua yields do not unwind C++ destructors. Continuations get a fresh scope.
class native_query_scope {
	duel* pd; lua_State* state; native_query_api api; card* target; bool open;
public:
	native_query_scope(duel* d, lua_State* L, native_query_api a, const card_set* members = nullptr, card* c = nullptr)
		: pd(d), state(L), api(a), target(c), open(bool(d->pool_observe)) {
		if(open) pd->pool_observe(state, api, true, members, target, -1);
	}
	void finish(const card_set* members = nullptr, int32_t result = -1) {
		if(open) { open = false; pd->pool_observe(state, api, false, members, target, result); }
	}
	~native_query_scope() { finish(); }
};

#endif /* DUEL_H_ */
