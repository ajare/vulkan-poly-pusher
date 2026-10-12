#include "mpp/app/PackageManifest.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace mpp::app
{
	namespace
	{
		bool safeRelativePath(std::string const& value)
		{
			std::filesystem::path path(value);if(value.empty()||path.is_absolute()||value.find(':')!=std::string::npos)return false;for(auto const& part:path)if(part=="..")return false;return true;
		}
		std::string extract(std::string const& text,std::string const& open,std::string const& close)
		{
			auto begin=text.find(open);if(begin==std::string::npos)throw std::runtime_error("Package manifest is missing "+open+".");begin+=open.size();auto end=text.find(close,begin);if(end==std::string::npos||text.find(open,end)!=std::string::npos)throw std::runtime_error("Package manifest has an invalid "+open+" element.");return text.substr(begin,end-begin);
		}
	}

	PackageManifest readPackageManifest(std::filesystem::path const& filename)
	{
		std::ifstream input(filename,std::ios::binary);if(!input)throw std::runtime_error("Package manifest is missing.");std::string text((std::istreambuf_iterator<char>(input)),{});constexpr char Root[]="<MppPackage version=\"1\">";if(text.rfind(Root,0)!=0||text.size()<sizeof(Root)-1||text.find("</MppPackage>")!=text.size()-(sizeof("</MppPackage>")-1))throw std::runtime_error("Package manifest is invalid or unsupported.");PackageManifest manifest;manifest.pipeline=extract(text,"<Pipeline>","</Pipeline>");manifest.scene=extract(text,"<Scene>","</Scene>");if(!safeRelativePath(manifest.pipeline)||!safeRelativePath(manifest.scene))throw std::runtime_error("Package manifest contains an unsafe document path.");return manifest;
	}

	void writePackageManifest(std::filesystem::path const& filename,PackageManifest const& manifest)
	{
		if(manifest.version!=PackageManifest::CurrentVersion||!safeRelativePath(manifest.pipeline)||!safeRelativePath(manifest.scene))throw std::runtime_error("Cannot write an invalid package manifest.");std::filesystem::create_directories(filename.parent_path());std::ofstream output(filename,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Could not write package manifest.");output<<"<MppPackage version=\"1\"><Pipeline>"<<manifest.pipeline<<"</Pipeline><Scene>"<<manifest.scene<<"</Scene></MppPackage>";if(!output)throw std::runtime_error("Could not finish package manifest.");
	}

	std::filesystem::path createUniqueTemporaryDirectory(std::string const& prefix)
	{
#ifdef _WIN32
		std::wstring root=std::filesystem::temp_directory_path().wstring();if(root.empty()||root.back()!=L'\\')root+=L'\\';wchar_t temporary[MAX_PATH]{};std::wstring wide(prefix.begin(),prefix.end());if(!GetTempFileNameW(root.c_str(),wide.substr(0,3).c_str(),0,temporary))throw std::runtime_error("Could not create a unique temporary package path.");std::filesystem::path result(temporary);std::filesystem::remove(result);std::filesystem::create_directory(result);return result;
#else
		for(unsigned index=0;index<1000;++index){auto path=std::filesystem::temp_directory_path()/(prefix+"-"+std::to_string(std::rand()));std::error_code error;if(std::filesystem::create_directory(path,error))return path;}throw std::runtime_error("Could not create a unique temporary package path.");
#endif
	}
}
