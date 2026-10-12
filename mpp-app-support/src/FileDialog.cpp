#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShObjIdl.h>
#endif
#include <SDL3/SDL.h>

#include <iterator>
#include <memory>
#include <mutex>

#include "mpp/app/FileDialog.h"

using namespace std;

namespace mpp::app
{
	struct AsyncParticleFileDialog::State
	{
		mutable std::mutex mutex;
		bool pending{ false };
		std::optional<AsyncFileDialogResult> result;
	};

	namespace
	{
		SDL_DialogFileFilter const particleFilters[]{
			{ "MPP particle effects", "particle.yaml" },
			{ "YAML files", "yaml;yml" },
			{ "All files", "*" }
		};

		void SDLCALL particleDialogCallback(void* userdata, char const* const* files, int)
		{
			std::unique_ptr<std::shared_ptr<AsyncParticleFileDialog::State>> holder(
				static_cast<std::shared_ptr<AsyncParticleFileDialog::State>*>(userdata));
			AsyncFileDialogResult result;
			if (!files)
				result.error = SDL_GetError();
			else if (files[0])
				result.path = files[0];
			std::lock_guard lock((*holder)->mutex);
			(*holder)->result = std::move(result);
			(*holder)->pending = false;
		}

		bool beginParticleDialog(std::shared_ptr<AsyncParticleFileDialog::State> const& state,
			SDL_Window* owner, std::string const& defaultLocation, bool save)
		{
			{
				std::lock_guard lock(state->mutex);
				if (state->pending) return false;
				state->pending = true;
				state->result.reset();
			}
			auto userdata = new std::shared_ptr<AsyncParticleFileDialog::State>(state);
			if (save)
				SDL_ShowSaveFileDialog(particleDialogCallback, userdata, owner, particleFilters,
					static_cast<int>(std::size(particleFilters)), defaultLocation.empty() ? nullptr : defaultLocation.c_str());
			else
				SDL_ShowOpenFileDialog(particleDialogCallback, userdata, owner, particleFilters,
					static_cast<int>(std::size(particleFilters)), nullptr, false);
			return true;
		}

		optional<string> show(SDL_Window* owner, string const& title, string const& defaultName, bool save, bool package=false, bool executable=false, bool image=false, bool gltf=false, bool folder=false, bool hdrExr=false)
		{
#ifndef _WIN32
			(void)owner; (void)title; (void)defaultName; (void)save; (void)package; (void)executable; (void)image; (void)gltf; (void)folder; (void)hdrExr;
			return {};
#else
			HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			IFileDialog* dialog = nullptr;
			HRESULT result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
			if (FAILED(result)) { if (SUCCEEDED(initialized)) CoUninitialize(); return {}; }
			wstring wideTitle(title.begin(), title.end()); dialog->SetTitle(wideTitle.c_str());
			if (folder)
			{
				FILEOPENDIALOGOPTIONS options{};
				dialog->GetOptions(&options);
				dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
			}
			else
			{
				COMDLG_FILTERSPEC filters[] = {
					{ executable ? L"Executable" : package ? L"MassivePolyPusher Package" : hdrExr ? L"OpenEXR HDR environment" : image ? L"Image files" : gltf ? L"glTF 2.0" : L"MassivePolyPusher Document", executable ? L"*.exe" : package ? L"*.mpppackage" : hdrExr ? L"*.exr" : image ? L"*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.gif;*.dds;*.hdr" : gltf ? L"*.gltf;*.glb" : L"*.yaml;*.xml" },
					{ L"All files", L"*.*" }
				};
				dialog->SetFileTypes(2, filters);
				dialog->SetDefaultExtension(executable ? L"exe" : package ? L"mpppackage" : image ? L"png" : gltf ? L"gltf" : L"yaml");
			}
			if (save && !defaultName.empty()) { wstring name(defaultName.begin(), defaultName.end()); dialog->SetFileName(name.c_str()); }
			// SDL3 replaced SDL_syswm.h with per-window properties.
			HWND window = owner ? (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(owner), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr) : nullptr;
			result = dialog->Show(window);
			optional<string> selected;
			if (SUCCEEDED(result))
			{
				IShellItem* item = nullptr; if (SUCCEEDED(dialog->GetResult(&item))) { PWSTR path = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr); string utf8(size, '\0'); WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr); utf8.pop_back(); selected = utf8; CoTaskMemFree(path); } item->Release(); }
			}
			dialog->Release(); if (SUCCEEDED(initialized)) CoUninitialize(); return selected;
#endif
		}
	}

	AsyncParticleFileDialog::AsyncParticleFileDialog() : mState(std::make_shared<State>()) {}

	bool AsyncParticleFileDialog::busy() const
	{
		std::lock_guard lock(mState->mutex);
		return mState->pending;
	}

	bool AsyncParticleFileDialog::open(SDL_Window* owner)
	{
		return beginParticleDialog(mState, owner, {}, false);
	}

	bool AsyncParticleFileDialog::save(SDL_Window* owner, std::string const& defaultLocation)
	{
		return beginParticleDialog(mState, owner, defaultLocation, true);
	}

	std::optional<AsyncFileDialogResult> AsyncParticleFileDialog::poll()
	{
		std::lock_guard lock(mState->mutex);
		if (!mState->result) return {};
		auto result = std::move(mState->result);
		mState->result.reset();
		return result;
	}

	optional<string> openXmlFileDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false); }
	optional<string> saveXmlFileDialog(SDL_Window* owner, string const& title, string const& defaultName) { return show(owner, title, defaultName, true); }
	optional<string> savePackageFileDialog(SDL_Window* owner, string const& title, string const& defaultName) { return show(owner, title, defaultName, true, true); }
	optional<string> openExecutableFileDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false, false, true); }
	optional<string> openImageFileDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false, false, false, true); }
	optional<string> openHdrExrFileDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false, false, false, false, false, false, true); }
	optional<string> openGltfFileDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false, false, false, false, true); }
	optional<string> selectFolderDialog(SDL_Window* owner, string const& title) { return show(owner, title, {}, false, false, false, false, false, true); }
}
