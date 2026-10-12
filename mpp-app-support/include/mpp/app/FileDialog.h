#pragma once

#include <memory>
#include <optional>
#include <string>

struct SDL_Window;

namespace mpp::app
{
	struct AsyncFileDialogResult
	{
		std::optional<std::string> path;
		std::string error;
	};

	// SDL's native dialogs are asynchronous and available on every platform SDL
	// supports. The shared mailbox keeps callback-thread data away from editor
	// state and remains alive if its owning application closes first.
	class AsyncParticleFileDialog
	{
	public:
		struct State;

	private:
		std::shared_ptr<State> mState;

	public:
		AsyncParticleFileDialog();
		bool busy() const;
		bool open(SDL_Window* owner);
		bool save(SDL_Window* owner, std::string const& defaultLocation);
		std::optional<AsyncFileDialogResult> poll();
	};

	std::optional<std::string> openXmlFileDialog(SDL_Window* owner, std::string const& title);
	std::optional<std::string> saveXmlFileDialog(SDL_Window* owner, std::string const& title, std::string const& defaultName);
	std::optional<std::string> savePackageFileDialog(SDL_Window* owner, std::string const& title, std::string const& defaultName);
	std::optional<std::string> openExecutableFileDialog(SDL_Window* owner, std::string const& title);
	std::optional<std::string> openImageFileDialog(SDL_Window* owner, std::string const& title);
	std::optional<std::string> openHdrExrFileDialog(SDL_Window* owner, std::string const& title);
	std::optional<std::string> openGltfFileDialog(SDL_Window* owner, std::string const& title);
	std::optional<std::string> selectFolderDialog(SDL_Window* owner, std::string const& title);
}
