#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "mpp/app/BackgroundWork.h"
#include "mpp/app/DocumentFile.h"

using namespace std;

namespace mpp::app
{
	namespace
	{
		struct CancellationState { atomic_bool cancelled{ false }; };
	}

	bool BackgroundCancellationToken::cancelled() const
	{
		auto state = static_pointer_cast<CancellationState>(mState.lock());
		return !state || state->cancelled.load(memory_order_relaxed);
	}

	void BackgroundCancellationToken::throwIfCancelled() const
	{
		if (cancelled()) throw runtime_error("Background operation cancelled.");
	}

	class BackgroundJobQueue::Implementation
	{
		struct Work
		{
			uint64_t generation;
			string label;
			Task task;
			shared_ptr<CancellationState> cancellation;
		};

		mutable mutex mMutex;
		condition_variable mCondition;
		deque<Work> mWork;
		deque<BackgroundJobResult> mResults;
		shared_ptr<CancellationState> mActiveCancellation;
		BackgroundJobProgress mProgress;
		uint64_t mGeneration{ 0 };
		bool mStopping{ false };
		thread mWorker;

		void run()
		{
			for (;;)
			{
				Work work;
				{
					unique_lock lock(mMutex);
					mCondition.wait(lock, [&] { return mStopping || !mWork.empty(); });
					if (mStopping) return;
					work = move(mWork.front());
					mWork.pop_front();
					mActiveCancellation = work.cancellation;
					if (work.generation == mGeneration)
						mProgress = { work.generation, work.label, "Starting", 0.0f, false, true };
				}

				BackgroundJobResult result;
				result.generation = work.generation;
				result.label = work.label;
				BackgroundCancellationToken token(work.cancellation);
				auto report = [this, generation = work.generation](float fraction, string stage)
				{
					lock_guard lock(mMutex);
					if (generation != mGeneration) return;
					mProgress.fraction = clamp(fraction, 0.0f, 1.0f);
					mProgress.stage = move(stage);
				};
				try
				{
					token.throwIfCancelled();
					result.value = work.task(token, report);
					result.cancelled = token.cancelled();
				}
				catch (exception const& error)
				{
					result.cancelled = token.cancelled();
					if (!result.cancelled) result.error = error.what();
				}
				catch (...)
				{
					result.cancelled = token.cancelled();
					if (!result.cancelled) result.error = "Unknown background operation failure.";
				}

				{
					lock_guard lock(mMutex);
					mResults.push_back(move(result));
					if (work.generation == mGeneration)
						mProgress = { work.generation, work.label, work.cancellation->cancelled.load() ? "Cancelled" : "Ready for GPU installation", 1.0f, false, false };
					mActiveCancellation.reset();
				}
			}
		}

	public:
		Implementation() : mWorker([this] { run(); }) {}
		~Implementation()
		{
			{
				lock_guard lock(mMutex);
				mStopping = true;
				if (mActiveCancellation) mActiveCancellation->cancelled = true;
				for (auto& work : mWork) work.cancellation->cancelled = true;
			}
			mCondition.notify_all();
			if (mWorker.joinable()) mWorker.join();
		}

		uint64_t submit(string label, Task task)
		{
			if (!task) throw invalid_argument("Background task is empty.");
			lock_guard lock(mMutex);
			if (mActiveCancellation) mActiveCancellation->cancelled = true;
			for (auto& old : mWork) old.cancellation->cancelled = true;
			mWork.clear();
			auto cancellation = make_shared<CancellationState>();
			auto generation = ++mGeneration;
			mWork.push_back({ generation, move(label), move(task), cancellation });
			mProgress = { generation, mWork.back().label, "Queued", 0.0f, true, false };
			mCondition.notify_one();
			return generation;
		}

		void cancel()
		{
			lock_guard lock(mMutex);
			++mGeneration;
			if (mActiveCancellation) mActiveCancellation->cancelled = true;
			for (auto& work : mWork) work.cancellation->cancelled = true;
			mWork.clear();
			mProgress = {};
		}

		bool poll(BackgroundJobResult& result)
		{
			lock_guard lock(mMutex);
			if (mResults.empty()) return false;
			result = move(mResults.front());
			mResults.pop_front();
			return true;
		}

		BackgroundJobProgress progress() const { lock_guard lock(mMutex); return mProgress; }
		uint64_t generation() const { lock_guard lock(mMutex); return mGeneration; }
	};

	BackgroundJobQueue::BackgroundJobQueue() : mImplementation(make_unique<Implementation>()) {}
	BackgroundJobQueue::~BackgroundJobQueue() = default;
	uint64_t BackgroundJobQueue::submit(string label, Task task) { return mImplementation->submit(move(label), move(task)); }
	void BackgroundJobQueue::cancel() { mImplementation->cancel(); }
	bool BackgroundJobQueue::poll(BackgroundJobResult& result) { return mImplementation->poll(result); }
	BackgroundJobProgress BackgroundJobQueue::progress() const { return mImplementation->progress(); }
	uint64_t BackgroundJobQueue::currentGeneration() const { return mImplementation->generation(); }

	class BackgroundFileWatcher::Implementation
	{
		struct Entry
		{
			DocumentFileRevision baseline;
			DocumentFileRevision observed;
			bool hasObserved{ false };
			bool emitted{ false };
		};

		mutex mMutex;
		condition_variable mCondition;
		map<filesystem::path, Entry> mFiles;
		deque<WatchedFileChange> mChanges;
		bool mStopping{ false };
		thread mWorker;

		void run()
		{
			for (;;)
			{
				{
					unique_lock lock(mMutex);
					if (mCondition.wait_for(lock, chrono::milliseconds(250), [&] { return mStopping; })) return;
				}
				vector<filesystem::path> paths;
				{
					lock_guard lock(mMutex);
					for (auto const& [path, unused] : mFiles) paths.push_back(path);
				}
				for (auto const& path : paths)
				{
					auto revision = captureDocumentFileRevision(path);
					lock_guard lock(mMutex);
					auto found = mFiles.find(path);
					if (found == mFiles.end()) continue;
					auto& entry = found->second;
					if (revision == entry.baseline) { entry.hasObserved = false; entry.emitted = false; continue; }
					if (!entry.hasObserved || !(revision == entry.observed))
					{
						entry.observed = revision; entry.hasObserved = true; entry.emitted = false;
					}
					else if (!entry.emitted)
					{
						mChanges.push_back({ path, revision.exists }); entry.emitted = true;
					}
				}
			}
		}

	public:
		Implementation() : mWorker([this] { run(); }) {}
		~Implementation()
		{
			{ lock_guard lock(mMutex); mStopping = true; }
			mCondition.notify_all();
			if (mWorker.joinable()) mWorker.join();
		}

		void setFiles(vector<filesystem::path> files)
		{
			lock_guard lock(mMutex);
			map<filesystem::path, Entry> replacement;
			for (auto& path : files)
			{
				path = normaliseDocumentPath(path);
				auto found = mFiles.find(path);
				replacement[path] = found == mFiles.end() ? Entry{ captureDocumentFileRevision(path) } : found->second;
			}
			mFiles = move(replacement);
			mChanges.erase(remove_if(mChanges.begin(), mChanges.end(), [&](auto const& change) { return !mFiles.contains(change.path); }), mChanges.end());
		}

		void acknowledge(filesystem::path path)
		{
			path = normaliseDocumentPath(path);
			auto revision = captureDocumentFileRevision(path);
			lock_guard lock(mMutex);
			auto found = mFiles.find(path);
			if (found != mFiles.end()) found->second = Entry{ revision };
			mChanges.erase(remove_if(mChanges.begin(), mChanges.end(), [&](auto const& change) { return change.path == path; }), mChanges.end());
		}

		void acknowledgeAll()
		{
			vector<filesystem::path> paths;
			{ lock_guard lock(mMutex); for (auto const& [path, unused] : mFiles) paths.push_back(path); }
			for (auto const& path : paths) acknowledge(path);
		}

		vector<WatchedFileChange> poll()
		{
			lock_guard lock(mMutex);
			vector<WatchedFileChange> result(make_move_iterator(mChanges.begin()), make_move_iterator(mChanges.end()));
			mChanges.clear();
			return result;
		}
	};

	BackgroundFileWatcher::BackgroundFileWatcher() : mImplementation(make_unique<Implementation>()) {}
	BackgroundFileWatcher::~BackgroundFileWatcher() = default;
	void BackgroundFileWatcher::setFiles(vector<filesystem::path> files) { mImplementation->setFiles(move(files)); }
	void BackgroundFileWatcher::acknowledge(filesystem::path const& path) { mImplementation->acknowledge(path); }
	void BackgroundFileWatcher::acknowledgeAll() { mImplementation->acknowledgeAll(); }
	vector<WatchedFileChange> BackgroundFileWatcher::poll() { return mImplementation->poll(); }
}
