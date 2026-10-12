#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace mpp::app
{
	struct PackageManifest
	{
		static constexpr uint32_t CurrentVersion{1};
		uint32_t version{CurrentVersion};
		std::string pipeline{"pipeline.yaml"};
		std::string scene{"scene.yaml"};
	};

	// Strict package-manifest I/O. Paths are package-relative and may not escape
	// the extracted package directory.
	PackageManifest readPackageManifest(std::filesystem::path const& filename);
	void writePackageManifest(std::filesystem::path const& filename,PackageManifest const& manifest={});

	// Creates a new empty directory below the operating system temporary folder.
	std::filesystem::path createUniqueTemporaryDirectory(std::string const& prefix);
}
