#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace mpp::app
{
	struct DocumentFileRevision
	{
		bool exists{ false };
		std::uintmax_t size{ 0 };
		std::filesystem::file_time_type writeTime{};
		std::uint64_t contentHash{ 0 };

		bool operator==(DocumentFileRevision const&) const = default;
	};

	std::filesystem::path normaliseDocumentPath(std::filesystem::path const& path);

	std::filesystem::path resolveDocumentReference(
		std::filesystem::path const& document,
		std::filesystem::path const& reference);

	std::filesystem::path makeDocumentRelativeReference(
		std::filesystem::path const& document,
		std::filesystem::path const& target);

	void atomicWriteText(std::filesystem::path const& destination, std::string const& text);

	DocumentFileRevision captureDocumentFileRevision(std::filesystem::path const& path);

	bool documentFileChanged(std::filesystem::path const& path, DocumentFileRevision const& baseline);

	std::filesystem::path documentRecoveryPath(std::filesystem::path const& document);

	bool documentHasNewerRecovery(std::filesystem::path const& document);

	bool removeDocumentRecovery(std::filesystem::path const& document) noexcept;
}
