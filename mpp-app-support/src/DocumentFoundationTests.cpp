#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <thread>
#include <utility>

#include "mpp/app/BackgroundWork.h"
#include "mpp/app/CommandStack.h"
#include "mpp/app/DocumentFile.h"
#include "mpp/app/DocumentFoundationTests.h"
#include "mpp/app/DocumentId.h"
#include "mpp/app/DocumentSnapshot.h"
#include "mpp/app/PackageManifest.h"
#include "mpp/app/ZipArchive.h"

using namespace std;

namespace mpp::app
{
	namespace
	{
		class SetIntCommand final : public EditorCommand
		{
			string mName;
			int* mTarget;
			int mBefore;
			int mAfter;

		public:
			SetIntCommand(string name, int* target, int after)
				: mName(move(name)), mTarget(target), mBefore(*target), mAfter(after) {}

			string const& name() const override { return mName; }
			void execute() override { *mTarget = mAfter; }
			void undo() override { *mTarget = mBefore; }
			bool merge(EditorCommand const& other) override
			{
				auto value = dynamic_cast<SetIntCommand const*>(&other);
				if (!value || value->mTarget != mTarget || value->mName != mName) return false;
				mAfter = value->mAfter;
				return true;
			}
		};
	}

	bool runDocumentFoundationTests(string* failure)
	{
		auto fail = [&](char const* message)
		{
			if (failure) *failure = message;
			return false;
		};

		if (!isValidDocumentId("Pass.Bloom-1") || isValidDocumentId("1Pass") || isValidDocumentId("Pass Bloom"))
			return fail("document ID validation is incorrect");
		DocumentIdRegistry ids;
		if (!ids.reserve("Pass") || ids.reserve("Pass") || ids.makeUnique("Pass") != "Pass.2")
			return fail("document ID uniqueness is incorrect");

		DocumentGeneration generations;
		auto value = make_shared<string const>("snapshot");
		DocumentSnapshot<string> snapshot(generations.next(), value);
		if (!snapshot || !generations.isCurrent(snapshot.generation()) || *snapshot.value() != "snapshot")
			return fail("immutable document snapshot generation is incorrect");
		generations.next();
		if (generations.isCurrent(snapshot.generation())) return fail("stale snapshot was accepted as current");

		int number = 0;
		CommandStack commands(3);
		commands.execute(make_unique<SetIntCommand>("One", &number, 1));
		commands.markSavePoint();
		commands.execute(make_unique<SetIntCommand>("Two", &number, 2));
		if (number != 2 || !commands.dirty() || !commands.canUndo()) return fail("command execution state is incorrect");
		commands.undo();
		if (number != 1 || commands.dirty() || !commands.canRedo()) return fail("command undo/save-point state is incorrect");
		commands.redo();
		if (number != 2) return fail("command redo state is incorrect");
		commands.undo();
		commands.execute(make_unique<SetIntCommand>("Branch", &number, 3));
		if (commands.canRedo() || !commands.dirty()) return fail("command branch did not discard redo/save state correctly");
		commands.clear(); number = 0;
		commands.execute(make_unique<SetIntCommand>("Drag", &number, 1), true);
		commands.execute(make_unique<SetIntCommand>("Drag", &number, 2), true);
		commands.execute(make_unique<SetIntCommand>("Drag", &number, 3), true);
		if (commands.size() != 1 || number != 3 || !commands.undo() || number != 0) return fail("continuous command coalescing failed");

		auto root = filesystem::temp_directory_path() / "mpp-document-foundation-tests";
		auto document = root / "documents" / "pipeline.xml";
		auto target = root / "assets" / "texture.png";
		auto relative = makeDocumentRelativeReference(document, target);
		if (resolveDocumentReference(document, relative) != normaliseDocumentPath(target))
			return fail("document-relative path round trip failed");
		atomicWriteText(document, "first");
		auto firstRevision = captureDocumentFileRevision(document);
		atomicWriteText(document, "other");
		if (!documentFileChanged(document, firstRevision)) return fail("external document change detection failed");
		atomicWriteText(document, "second");
		auto recovery = documentRecoveryPath(document);
		atomicWriteText(recovery, "recovered");
		error_code recoveryTimeError;
		filesystem::last_write_time(recovery, filesystem::last_write_time(document) + chrono::seconds(2), recoveryTimeError);
		if (recoveryTimeError || !documentHasNewerRecovery(document) || !removeDocumentRecovery(document) || filesystem::exists(recovery))
			return fail("document recovery lifecycle failed");
		auto missingDocument = root / "documents" / "missing.xml";
		atomicWriteText(documentRecoveryPath(missingDocument), "unsaved recovery");
		if (!documentHasNewerRecovery(missingDocument) || !removeDocumentRecovery(missingDocument))
			return fail("recovery without an explicit save was not detected");
		ifstream input(document, ios::binary);
		string contents((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
		input.close();
		error_code ignored;
		filesystem::remove_all(root, ignored);
		if (contents != "second") return fail("atomic document replacement failed");

		// Package archives are ZIP-store files, but must preserve binary payloads and
		// reject path traversal before an archive reaches a workspace loader.
		auto zipSource = root / "zip-source.bin"; atomicWriteText(document, "second"); atomicWriteText(zipSource, "package payload");
		auto archive = root / "workspace.mpppackage", extracted = root / "extracted", manifestFile=root / "manifest.xml";
		writePackageManifest(manifestFile);auto manifest=readPackageManifest(manifestFile);if(manifest.pipeline!="pipeline.yaml"||manifest.scene!="scene.yaml")return fail("package manifest round trip failed");
		ZipArchive::write(archive, { { "manifest.xml", document }, { "assets/payload.bin", zipSource } });
		ZipArchive::extract(archive, extracted);
		// Replacing an existing export must be atomic on Windows as well.
		atomicWriteText(zipSource, "updated package payload"); ZipArchive::write(archive, { { "manifest.xml", document }, { "assets/payload.bin", zipSource } }); filesystem::remove_all(extracted, ignored); ZipArchive::extract(archive, extracted);
		ifstream payload(extracted / "assets" / "payload.bin", ios::binary);
		string payloadText((istreambuf_iterator<char>(payload)), istreambuf_iterator<char>());
		if (payloadText != "updated package payload" || !filesystem::exists(extracted / "manifest.xml")) return fail("ZIP package round trip failed");
		bool rejectedUnsafeZip=false;try{ZipArchive::write(root / "unsafe.mpppackage",{{"../outside.txt",zipSource}});}catch(runtime_error const&){rejectedUnsafeZip=true;}if(!rejectedUnsafeZip)return fail("ZIP package path traversal was accepted");
		map<string,filesystem::path> excessiveEntries;for(unsigned index=0;index<4097;++index)excessiveEntries.emplace("assets/"+to_string(index),zipSource);bool rejectedExcessive=false;try{ZipArchive::write(root / "excessive.mpppackage",excessiveEntries);}catch(runtime_error const&){rejectedExcessive=true;}if(!rejectedExcessive)return fail("ZIP package entry limit was not enforced");

		BackgroundJobQueue jobs;
		atomic_bool firstStarted{ false };
		auto obsoleteGeneration = jobs.submit("Obsolete", [&](BackgroundCancellationToken const& cancellation, BackgroundJobQueue::ProgressCallback const&)
		{
			firstStarted = true;
			while (!cancellation.cancelled()) this_thread::yield();
			cancellation.throwIfCancelled();
			return any();
		});
		for (int spin = 0; spin < 1000 && !firstStarted; ++spin) this_thread::sleep_for(chrono::milliseconds(1));
		if (!firstStarted) return fail("background worker did not start");
		auto currentGeneration = jobs.submit("Current", [](BackgroundCancellationToken const& cancellation, BackgroundJobQueue::ProgressCallback const& progress)
		{
			progress(0.5f, "Testing"); cancellation.throwIfCancelled(); return any(42);
		});
		BackgroundJobResult jobResult; bool receivedCurrent = false, receivedCancelled = false;
		for (int spin = 0; spin < 2000 && !receivedCurrent; ++spin)
		{
			while (jobs.poll(jobResult))
			{
				if (jobResult.generation == obsoleteGeneration) receivedCancelled = jobResult.cancelled;
				if (jobResult.generation == currentGeneration) receivedCurrent = !jobResult.cancelled && jobResult.error.empty() && any_cast<int>(jobResult.value) == 42;
			}
			this_thread::sleep_for(chrono::milliseconds(1));
		}
		if (!receivedCancelled || !receivedCurrent || jobs.currentGeneration() != currentGeneration)
			return fail("background cancellation or stale-generation rejection failed");

		auto watched = root / "watch" / "asset.txt";
		atomicWriteText(watched, "one");
		BackgroundFileWatcher watcher; watcher.setFiles({ watched });
		atomicWriteText(watched, "two");
		bool observedChange = false;
		for (int spin = 0; spin < 20 && !observedChange; ++spin)
		{
			this_thread::sleep_for(chrono::milliseconds(100));
			for (auto const& change : watcher.poll()) if (change.path == normaliseDocumentPath(watched)) observedChange = true;
		}
		if (!observedChange) return fail("background stable file watching failed");
		watcher.acknowledge(watched); atomicWriteText(watched, "own save"); watcher.acknowledge(watched);
		this_thread::sleep_for(chrono::milliseconds(600));
		if (!watcher.poll().empty()) return fail("file watcher own-save suppression failed");
		filesystem::remove_all(root, ignored);

		return true;
	}
}
