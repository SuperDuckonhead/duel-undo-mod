#include "test_support.h"
#include "ocgapi.h"
#include "duel.h"
#include "interpreter.h"
#include "lstate.h"
#include <cstring>
#include <iostream>
static byte* script(const char* name, int* len) {
 static char bootstrap[] = "first = math.random(1,2147483647); math.randomseed(); second = math.random(1,2147483647); math.randomseed(12,34); explicit = math.random(1,2147483647)";
 static char empty[] = ""; char* bytes = std::strstr(name,"constant.lua") ? bootstrap : empty;
 *len = std::strlen(bytes); return reinterpret_cast<byte*>(bytes);
}
static lua_Integer number(intptr_t handle, const char* key) {
 auto state = reinterpret_cast<duel*>(handle)->lua->lua_state;
 lua_getglobal(state,key); auto n = lua_tointeger(state,-1); lua_pop(state,1); return n;
}
int main(int argc, char**) {
 try {
  set_script_reader(script); uint32_t seed[SEED_COUNT]{}; seed[0]=123;
#ifdef UNDO_RNG_API
  auto a=create_duel_undo(seed); auto b=create_duel_undo(seed);
#else
  auto a=create_duel_v2(seed); auto b=create_duel_v2(seed);
#endif
  auto legacy=create_duel_v2(seed);
  CHECK(number(a,"explicit")==number(legacy,"explicit"));
  CHECK(reinterpret_cast<duel*>(a)->get_next_integer(1,1000000)==reinterpret_cast<duel*>(legacy)->get_next_integer(1,1000000));
  end_duel(legacy);
  auto firstA=number(a,"first"), firstB=number(b,"first"), secondA=number(a,"second"), secondB=number(b,"second");
  auto hashA=G(reinterpret_cast<duel*>(a)->lua->lua_state)->seed; auto hashB=G(reinterpret_cast<duel*>(b)->lua->lua_state)->seed;
  end_duel(a); end_duel(b);
  std::cout << "Lua hash seeds: " << hashA << " / " << hashB << "\n"; CHECK(hashA==hashB);
  std::cout << firstA << ' ' << firstB << " / " << secondA << ' ' << secondB << '\n';
  CHECK(firstA==firstB); CHECK(secondA==secondB);
 } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}