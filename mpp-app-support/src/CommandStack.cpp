#include <algorithm>
#include <stdexcept>
#include <utility>

#include "mpp/app/CommandStack.h"

using namespace std;

namespace mpp::app
{
	CommandStack::CommandStack(size_t limit)
		: mLimit(max<size_t>(1, limit))
	{
	}

	void CommandStack::trimToLimit()
	{
		if (mCommands.size() <= mLimit) return;
		size_t removeCount = mCommands.size() - mLimit;
		mCommands.erase(mCommands.begin(), mCommands.begin() + removeCount);
		mCursor -= min(mCursor, removeCount);
		if (mSavePointReachable)
		{
			if (mSaveCursor < removeCount) mSavePointReachable = false;
			else mSaveCursor -= removeCount;
		}
	}

	void CommandStack::execute(unique_ptr<EditorCommand> command, bool coalesce)
	{
		if (!command) throw invalid_argument("CommandStack cannot execute a null command.");
		if (coalesce && mCoalescing && mCursor == mCommands.size() && mCursor > 0 && mCommands.back()->merge(*command))
		{
			mCommands.back()->execute();
			return;
		}
		mCoalescing = coalesce;
		if (mCursor < mCommands.size())
		{
			if (mSavePointReachable && mSaveCursor > mCursor) mSavePointReachable = false;
			mCommands.erase(mCommands.begin() + mCursor, mCommands.end());
		}
		command->execute();
		mCommands.push_back(move(command));
		mCursor = mCommands.size();
		trimToLimit();
	}

	void CommandStack::endCoalescing() { mCoalescing = false; }

	bool CommandStack::canUndo() const { return mCursor > 0; }

	bool CommandStack::canRedo() const { return mCursor < mCommands.size(); }

	string const* CommandStack::undoName() const
	{
		return canUndo() ? &mCommands[mCursor - 1]->name() : nullptr;
	}

	string const* CommandStack::redoName() const
	{
		return canRedo() ? &mCommands[mCursor]->name() : nullptr;
	}

	bool CommandStack::undo()
	{
		mCoalescing = false;
		if (!canUndo()) return false;
		mCommands[mCursor - 1]->undo();
		--mCursor;
		return true;
	}

	bool CommandStack::redo()
	{
		mCoalescing = false;
		if (!canRedo()) return false;
		mCommands[mCursor]->execute();
		++mCursor;
		return true;
	}

	void CommandStack::markSavePoint()
	{
		mSaveCursor = mCursor;
		mSavePointReachable = true;
		mCoalescing = false;
	}

	bool CommandStack::dirty() const
	{
		return !mSavePointReachable || mCursor != mSaveCursor;
	}

	void CommandStack::clear()
	{
		mCommands.clear();
		mCursor = 0;
		mSaveCursor = 0;
		mSavePointReachable = true;
		mCoalescing = false;
	}

	size_t CommandStack::size() const { return mCommands.size(); }

	size_t CommandStack::cursor() const { return mCursor; }
}
