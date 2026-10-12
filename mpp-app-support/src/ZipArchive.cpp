#include "mpp/app/ZipArchive.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace mpp::app
{
	namespace
	{
		uint32_t crc32(std::vector<char> const& data)
		{
			uint32_t result=0xffffffffu;
			for(unsigned char value:data){result^=value;for(int bit=0;bit<8;++bit)result=(result>>1)^((result&1)?0xedb88320u:0);}
			return result^0xffffffffu;
		}
		void put16(std::ostream& out,uint16_t value){out.put((char)value);out.put((char)(value>>8));}
		void put32(std::ostream& out,uint32_t value){put16(out,(uint16_t)value);put16(out,(uint16_t)(value>>16));}
		uint16_t get16(std::istream& in){auto a=in.get(),b=in.get();if(a<0||b<0)throw std::runtime_error("Truncated ZIP archive.");return (uint16_t)(a|(b<<8));}
		uint32_t get32(std::istream& in){auto a=get16(in),b=get16(in);return a|((uint32_t)b<<16);}
		void require(uint32_t actual,uint32_t expected){if(actual!=expected)throw std::runtime_error("Invalid ZIP archive.");}
		std::vector<char> read(std::filesystem::path const& file)
		{
			std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Could not read package payload '"+file.string()+"'.");
			return {std::istreambuf_iterator<char>(in),{}};
		}
		bool safeName(std::string const& name)
		{
			std::filesystem::path path(name);if(name.empty()||path.is_absolute()||name.find(':')!=std::string::npos)return false;for(auto const& part:path)if(part=="..")return false;return true;
		}
		constexpr uint16_t MaxEntries=4096;
		constexpr uint32_t MaxEntryBytes=512u*1024u*1024u;
		constexpr uint64_t MaxArchiveBytes=2ull*1024u*1024u*1024u;
		struct Entry {std::string name;uint32_t crc,size,offset;};
	}

	void ZipArchive::write(std::filesystem::path const& archive,std::map<std::string,std::filesystem::path> const& files)
	{
		if(files.empty()||files.size()>MaxEntries)throw std::runtime_error("Package has an invalid number of entries.");
		std::filesystem::create_directories(archive.parent_path());auto temporary=archive;temporary+=".tmp";std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Could not create package '"+archive.string()+"'.");
		std::vector<Entry> entries;uint64_t totalBytes=0;
		for(auto const& [name,file]:files)
		{
			if(!safeName(name)||name.size()>0xffff)throw std::runtime_error("Invalid package entry name '"+name+"'.");auto bytes=read(file);if(bytes.size()>MaxEntryBytes||(totalBytes+=bytes.size())>MaxArchiveBytes)throw std::runtime_error("Package entry is too large: '"+name+"'.");auto offset=(uint32_t)out.tellp();auto crc=crc32(bytes);put32(out,0x04034b50);put16(out,20);put16(out,0);put16(out,0);put16(out,0);put16(out,0);put32(out,crc);put32(out,(uint32_t)bytes.size());put32(out,(uint32_t)bytes.size());put16(out,(uint16_t)name.size());put16(out,0);out.write(name.data(),name.size());if(!bytes.empty())out.write(bytes.data(),bytes.size());entries.push_back({name,crc,(uint32_t)bytes.size(),offset});
		}
		auto centralOffset=(uint32_t)out.tellp();for(auto const& entry:entries){put32(out,0x02014b50);put16(out,20);put16(out,20);put16(out,0);put16(out,0);put16(out,0);put16(out,0);put32(out,entry.crc);put32(out,entry.size);put32(out,entry.size);put16(out,(uint16_t)entry.name.size());put16(out,0);put16(out,0);put16(out,0);put16(out,0);put32(out,0);put32(out,entry.offset);out.write(entry.name.data(),entry.name.size());}auto centralSize=(uint32_t)out.tellp()-centralOffset;put32(out,0x06054b50);put16(out,0);put16(out,0);put16(out,(uint16_t)entries.size());put16(out,(uint16_t)entries.size());put32(out,centralSize);put32(out,centralOffset);put16(out,0);out.close();if(!out)throw std::runtime_error("Could not finish package '"+archive.string()+"'.");
#ifdef _WIN32
		if(!MoveFileExW(temporary.c_str(),archive.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temporary);throw std::runtime_error("Could not replace package '"+archive.string()+"'.");}
#else
		std::error_code error;std::filesystem::rename(temporary,archive,error);if(error){std::filesystem::remove(temporary);throw std::runtime_error("Could not replace package '"+archive.string()+"'.");}
#endif
	}

	void ZipArchive::extract(std::filesystem::path const& archive,std::filesystem::path const& destination)
	{
		std::ifstream in(archive,std::ios::binary);if(!in)throw std::runtime_error("Could not open package '"+archive.string()+"'.");in.seekg(0,std::ios::end);std::streamoff length=(std::streamoff)in.tellg();if(length<22)throw std::runtime_error("Package has no ZIP directory.");auto begin=std::max<std::streamoff>(0,length-0x10016);in.seekg(begin);std::vector<char> tail((size_t)(length-begin));in.read(tail.data(),tail.size());size_t end=std::string(tail.data(),tail.size()).rfind("PK\005\006");if(end==std::string::npos)throw std::runtime_error("Package ZIP directory is missing.");in.seekg(begin+(std::streamoff)end+4);get16(in);get16(in);auto count=get16(in);require(get16(in),count);auto centralSize=get32(in),centralOffset=get32(in);(void)centralSize;if(count==0||count>MaxEntries)throw std::runtime_error("Package has an invalid number of entries.");in.seekg(centralOffset);std::filesystem::create_directories(destination);std::set<std::string> names;uint64_t totalBytes=0;
		for(uint16_t index=0;index<count;++index){require(get32(in),0x02014b50);get16(in);get16(in);get16(in);auto method=get16(in);get16(in);get16(in);auto crc=get32(in),compressed=get32(in),size=get32(in);auto nameLength=get16(in),extra=get16(in),comment=get16(in);get16(in);get16(in);get32(in);auto offset=get32(in);std::string name(nameLength,'\0');in.read(name.data(),nameLength);in.seekg(extra+comment,std::ios::cur);if(method!=0||compressed!=size||size>MaxEntryBytes||!safeName(name)||!names.insert(name).second||(totalBytes+=size)>MaxArchiveBytes)throw std::runtime_error("Package contains an unsupported, oversized, duplicate, or unsafe ZIP entry.");auto returnAt=in.tellg();in.seekg(offset);require(get32(in),0x04034b50);get16(in);get16(in);require(get16(in),0);get16(in);get16(in);require(get32(in),crc);require(get32(in),size);require(get32(in),size);auto localName=get16(in),localExtra=get16(in);in.seekg(localName+localExtra,std::ios::cur);std::vector<char> bytes(size);if(size)in.read(bytes.data(),size);if(!in||crc32(bytes)!=crc)throw std::runtime_error("Package payload CRC check failed.");auto target=destination/name;std::filesystem::create_directories(target.parent_path());std::ofstream out(target,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("Could not extract package payload.");in.clear();in.seekg(returnAt);}
	}
}
