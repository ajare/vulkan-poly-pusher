#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace mpp::app
{
	// Minimal standards-compliant ZIP implementation for package files. Archives
	// are written with the ZIP "stored" method; extraction deliberately rejects
	// compressed and path-traversal entries.
	class ZipArchive
	{
	public:
		static void write(std::filesystem::path const& archive, std::map<std::string,std::filesystem::path> const& entries);
		static void extract(std::filesystem::path const& archive, std::filesystem::path const& destination);
	};
}
