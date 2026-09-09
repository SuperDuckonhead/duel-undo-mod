#pragma once
#include "test_support.h"
#include "undo/resource_view.h"
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
namespace fixture {
namespace fs=std::filesystem;
using undo::Bytes;
inline Bytes bytes(const std::string& s) {return Bytes(s.begin(),s.end());}
inline void WriteFixtureFile(const std::string& path,const Bytes& bytes) {
 fs::create_directories(fs::u8path(path).parent_path());
 std::ofstream f(fs::u8path(path),std::ios::binary|std::ios::trunc);
 f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); if(!f)throw std::runtime_error("Fixture write failed: "+path);
}
inline void sql(const std::string& path,const std::string& command) {
 sqlite3* db{};CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);char* error{};
 auto status=sqlite3_exec(db,command.c_str(),nullptr,nullptr,&error);
 std::string message=error?error:"";sqlite3_free(error);sqlite3_close(db);
 if(status!=SQLITE_OK)throw std::runtime_error(message);
}
inline void database(const std::string& root) {
 fs::create_directories(root);
 sql(root+"/cards.cdb","DROP TABLE IF EXISTS datas; DROP TABLE IF EXISTS texts; CREATE TABLE datas(id INTEGER PRIMARY KEY,ot INTEGER,alias INTEGER,setcode INTEGER,type INTEGER,atk INTEGER,def INTEGER,level INTEGER,race INTEGER,attribute INTEGER,category INTEGER); CREATE TABLE texts(id INTEGER PRIMARY KEY,name TEXT,desc TEXT,str1 TEXT,str2 TEXT,str3 TEXT,str4 TEXT,str5 TEXT,str6 TEXT,str7 TEXT,str8 TEXT,str9 TEXT,str10 TEXT,str11 TEXT,str12 TEXT,str13 TEXT,str14 TEXT,str15 TEXT,str16 TEXT); INSERT INTO datas VALUES(900000001,0,0,4660,17,1800,1200,4,1,1,0); INSERT INTO texts(id,name,desc) VALUES(900000001,'fixture','normal monster');");
}
inline void word(Bytes& b,uint32_t n,unsigned width=4) {for(unsigned i=0;i<width;++i)b.push_back(static_cast<uint8_t>(n>>(i*8)));}
inline void zip(const std::string& path,const std::string& name,const Bytes& data) {
 uint32_t crc=~0u;for(auto c:data){crc^=c;for(int j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}crc=~crc;
 Bytes b;word(b,0x04034b50);word(b,20,2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,crc);word(b,data.size());word(b,data.size());word(b,name.size(),2);word(b,0,2);b.insert(b.end(),name.begin(),name.end());b.insert(b.end(),data.begin(),data.end());
 auto central=b.size();word(b,0x02014b50);word(b,20,2);word(b,20,2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,crc);word(b,data.size());word(b,data.size());word(b,name.size(),2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,0,2);word(b,0);word(b,0);b.insert(b.end(),name.begin(),name.end());
 auto centralSize=b.size()-central;word(b,0x06054b50);word(b,0,2);word(b,0,2);word(b,1,2);word(b,1,2);word(b,centralSize);word(b,central);word(b,0,2);WriteFixtureFile(path,b);
}
}