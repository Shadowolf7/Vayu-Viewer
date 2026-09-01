/**
 * @file hbexternaleditor.cpp
 * @brief Utility class to launch an external program for editing a file and
 * tracking changes on the latter.
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 *
 * Copyright (c) 2019-2026, Henri Beauchamp.
 *
 * Second Life Viewer Source Code
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; version 2.1 of the License only.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA"
 * $/LicenseInfo$
 */

#include "linden_common.h"

#if LL_LINUX
# include <stdlib.h> // For getenv()
#endif

#include "hbexternaleditor.h"

#include "llfile.h"
#include "lllivefile.h"
#include "llprocess.h"
#include "lltrans.h"
#include "llfloater.h"

///////////////////////////////////////////////////////////////////////////////
// HBEditorLiveFile class, for edited file live tracking
///////////////////////////////////////////////////////////////////////////////

class HBEditorLiveFile final : public LLLiveFile
{
public:
	HBEditorLiveFile(HBExternalEditor* editor, const std::string& filename)
	:	LLLiveFile(filename, 1.f),
		mEditor(editor)
	{
	}

protected:
	bool loadFile() override
	{
		if (mEditor)
		{
			mEditor->callChangedCallback(filename());
		}
		return true;
	}

private:
	HBExternalEditor* mEditor;
};

///////////////////////////////////////////////////////////////////////////////
// HBExternalEditor class proper
///////////////////////////////////////////////////////////////////////////////

HBExternalEditor::HBExternalEditor(HBExternalEditorFileChangedCB callback,
								   void* userdata, bool orphanize_on_destroy)
:	mFiledChangedCallback(callback),
	mUserData(userdata),
	mIgnoreNextUpdate(false),
	mEditorIsDetached(false),
	mOrphanizeOnDestroy(orphanize_on_destroy),
	mProcess(nullptr),
	mEditedFile(nullptr)
{
}

HBExternalEditor::~HBExternalEditor()
{
	if (mEditedFile)
	{
		delete mEditedFile;
		mEditedFile = nullptr;
	}
	if (mProcess)
	{
		if (!mOrphanizeOnDestroy)
		{
			mProcess->kill();
		}
		mProcess.reset();
	}
}

void HBExternalEditor::callChangedCallback(const std::string& filename)
{
	if (!mIgnoreNextUpdate && mFiledChangedCallback)
	{
		mFiledChangedCallback(filename, mUserData);
	}
	mIgnoreNextUpdate = false;
}

bool HBExternalEditor::open(const std::string& filename, std::string cmd)
{
	if (!LLFile::isfile(filename))
	{
		mErrorMessage = LLTrans::getString("file_not_found") + " " + filename;
		LL_WARNS() << mErrorMessage << LL_ENDL;
		return false;
	}

	mEditorIsDetached = false;
	if (cmd.empty())
	{
		LLControlGroup* controls = LLFloater::getControlGroup();
		if (controls && controls->controlExists("ExternalEditor"))
		{
			cmd = controls->getString("ExternalEditor");
		}
	}
	if (cmd.empty())
	{
#if LL_LINUX
		LL_WARNS() << "Could not find a configured editor; trying 'xdg-open'. This is suboptimal because the state of the editor it will launch cannot be tracked. Please, consider configuring the \"ExternalEditor\" setting."
				   << LL_ENDL;
		cmd = "/usr/bin/xdg-open %s";
		mEditorIsDetached = true;
#elif LL_WINDOWS
		LL_WARNS() << "Could not find a configured editor; trying 'explorer.exe' to dispatch to the system editor."
				   << LL_ENDL;
		cmd = "%SystemRoot%\\explorer.exe \"%s\"";
		mEditorIsDetached = true;
#elif LL_DARWIN
		LL_WARNS() << "Could not find a configured editor; trying '/usr/bin/open'."
				   << LL_ENDL;
		cmd = "/usr/bin/open \"%s\"";
		mEditorIsDetached = true;
#endif
	}
	LLStringUtil::trim(cmd);
	if (cmd.empty())
	{
		mErrorMessage = LLTrans::getString("ExternalEditorNotSet");
		LL_WARNS() << mErrorMessage << LL_ENDL;
		return false;
	}

	// Split the command line between program file name and arguments
	std::string prg;
	size_t i;
	if (cmd[0] == '"')
	{
		// Starting with a quoted program name, as often seen under Windows,
		// because of spaces in the path.
		i = cmd.find('"', 1);	// Find the matching closing quote
		if (i == std::string::npos)
		{
			mErrorMessage = LLTrans::getString("ExternalEditorCommandParseError");
			LL_WARNS() << mErrorMessage << LL_ENDL;
			return false;
		}
		prg = cmd.substr(1, i - 1);
		cmd = cmd.substr(i + 1);
	}
	else
	{
		i = cmd.find(' ', 1);	// Find the first space
		if (i == std::string::npos)
		{
			// No argument, just a program...
			prg = cmd;
			cmd.clear();
		}
		else
		{
			prg = cmd.substr(0, i);
			cmd = cmd.substr(i + 1);
		}
	}
	if (cmd.find("%s") == std::string::npos)
	{
		// Add the filename if absent from the arguments
#if LL_WINDOWS
		cmd += " \"%s\"";
#else
		cmd += " %s";
#endif
	}
	LLStringUtil::trimHead(cmd);

	LL_INFOS() << "Using external editor command line: " << prg << " " << cmd
			   << LL_ENDL;

	if (mEditedFile)
	{
		delete mEditedFile;
		mEditedFile = nullptr;
	}
	// Watch as live file only if we got a "file changed" event callback
	if (mFiledChangedCallback)
	{
		mEditedFile = new HBEditorLiveFile(this, filename);
		mEditedFile->addToEventTimer();
	}

	std::vector<std::string> tokens;
	LLStringUtil::getTokens(cmd, tokens, " ");

	LLProcess::Params params;
	params.executable = prg;
	params.autokill = !mOrphanizeOnDestroy;
	params.attached = !mOrphanizeOnDestroy;

	for (size_t tok_idx = 0; tok_idx < tokens.size(); ++tok_idx)
	{
		std::string parameter = tokens[tok_idx];
		if (!parameter.empty())
		{
#if LL_LINUX
			// Under POSIX operating systems, arguments for execv() are passed
			// in the argv array and none need quoting
			LLStringUtil::replaceString(parameter, "\"%s\"", filename);
#endif
			LLStringUtil::replaceString(parameter, "%s", filename);
			params.args.add(parameter);
		}
	}

	if (mProcess)
	{
		if (!mOrphanizeOnDestroy)
		{
			mProcess->kill();
		}
		mProcess.reset();
	}

	mProcess = LLProcess::create(params);
	if (!mProcess)
	{
		mErrorMessage = LLTrans::getString("ExternalEditorFailedToRun") + " " + prg;
		LL_WARNS() << mErrorMessage << LL_ENDL;
		kill();
		return false;
	}

	// Opening the file in the external editor caused it to be touched and we
	// do not want to trigger a "file changed" event for this...
	mIgnoreNextUpdate = true;

	return true;
}

void HBExternalEditor::kill()
{
	if (mEditedFile)
	{
		delete mEditedFile;
		mEditedFile = nullptr;
	}
	if (mProcess)
	{
		if (mEditorIsDetached)
		{
			LL_WARNS() << "Cannot kill a detached editor process..." << LL_ENDL;
		}
		else
		{
			mProcess->kill();
		}
		mProcess.reset();
	}
}

bool HBExternalEditor::running()
{
	return mProcess && (mEditorIsDetached || mProcess->isRunning());
}

std::string HBExternalEditor::getFilename()
{
	return mEditedFile ? mEditedFile->filename() : LLStringUtil::null;
}
