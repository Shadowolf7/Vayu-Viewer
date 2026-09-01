/**
 * @file hbexternaleditor.h
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

#pragma once

#include "linden_common.h"
#include "llstring.h"
#include "llprocess.h"

class LLLiveFile;

class HBExternalEditor
{
	friend class HBEditorLiveFile;

protected:
	LOG_CLASS(HBExternalEditor);

public:
	typedef void (*HBExternalEditorFileChangedCB)(const std::string& filename,
												  void* userdata);
	HBExternalEditor(HBExternalEditorFileChangedCB callback,
					 void* userdata = nullptr,
					 bool orphanize_on_destroy = false);
	~HBExternalEditor();

	// Call with the name of the file to edit and watch, as well as an optional
	// command line (with "%s" as the string argument symbol that will be
	// replaced with the filename). E.g.: /usr/bin/nedit %s
	// Returns true on success, false on error (error message set accordingly).
	bool open(const std::string& filename,
			  std::string command_line = LLStringUtil::null);

	// Call to attempt to kill the external editor (also closes the live file)
	void kill();

	// Returns true when the external editor is still running, or when we know
	// for sure that the editor is detached from the original (and now gone)
	// launched process, which happens when we launch a MIME wrapper launcher
	// instead of the actual editor.
	bool running();

	// Returns the last error message.
	inline const std::string& getErrorMessage()	{ return mErrorMessage; }

	// Call this when planning to update the file yourself and not wanting
	// to get notified uselessly about it via the changed callback.
	inline void ignoreNextUpdate()				{ mIgnoreNextUpdate = true; }

	std::string getFilename();

private:
	// For use by HBEditorLiveFile only
	void callChangedCallback(const std::string& filename);

private:
	void				(*mFiledChangedCallback)(const std::string& filename,
												 void* userdata);
	void*				mUserData;
	LLProcessPtr		mProcess;
	LLLiveFile*			mEditedFile;
	std::string			mErrorMessage;
	bool				mOrphanizeOnDestroy;
	bool				mIgnoreNextUpdate;
	bool				mEditorIsDetached;
};
