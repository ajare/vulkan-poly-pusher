#pragma once

#include <any>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mpp::app
{
	class BackgroundCancellationToken
	{
		std::weak_ptr<void> mState;

		explicit BackgroundCancellationToken(std::shared_ptr<void> state) : mState(std::move(state)) {}
		friend class BackgroundJobQueue;

	public:
		BackgroundCancellationToken() = default;
		bool cancelled() const;
		void throwIfCancelled() const;
	};

	struct BackgroundJobProgress
	{
		uint64_t generation{ 0 };
		std::string label;
		std::string stage;
		float fraction{ 0.0f };
		bool queued{ false };
		bool running{ false };
	};

	struct BackgroundJobResult
	{
		uint64_t generation{ 0 };
		std::string label;
		std::any value;
		std::string error;
		bool cancelled{ false };
	};

	class BackgroundJobQueue
	{
		class Implementation;
		std::unique_ptr<Implementation> mImplementation;

	public:
		using ProgressCallback = std::function<void(float, std::string)>;
		using Task = std::function<std::any(BackgroundCancellationToken const&, ProgressCallback const&)>;

		BackgroundJobQueue();
		~BackgroundJobQueue();
		BackgroundJobQueue(BackgroundJobQueue const&) = delete;
		BackgroundJobQueue& operator=(BackgroundJobQueue const&) = delete;

		// Submitting newer work cancels queued/running older generations.
		uint64_t submit(std::string label, Task task);
		void cancel();
		bool poll(BackgroundJobResult& result);
		BackgroundJobProgress progress() const;
		uint64_t currentGeneration() const;
	};

	struct WatchedFileChange
	{
		std::filesystem::path path;
		bool exists{ false };
	};

	// Content-aware polling watcher. Changes are emitted only after one stable poll,
	// avoiding reloads of partially replaced files.
	class BackgroundFileWatcher
	{
		class Implementation;
		std::unique_ptr<Implementation> mImplementation;

	public:
		BackgroundFileWatcher();
		~BackgroundFileWatcher();
		BackgroundFileWatcher(BackgroundFileWatcher const&) = delete;
		BackgroundFileWatcher& operator=(BackgroundFileWatcher const&) = delete;

		void setFiles(std::vector<std::filesystem::path> files);
		void acknowledge(std::filesystem::path const& path);
		void acknowledgeAll();
		std::vector<WatchedFileChange> poll();
	};
}
