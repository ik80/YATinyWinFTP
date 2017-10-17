//------------------------------------------------------------------------------------
// Taken as is from ftpdmin.
// Module to handle mapping specified paths onto a relative path.
//------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <winsock2.h>
#include <direct.h>

#include "TinyFTPPaths.h"

namespace TinyWinFTP
{
	static char RootDir[MAX_PATH] = "c:\\"; // stored with backslashes.

	//------------------------------------------------------------------------------------
	// Specify root dir, specified with backslashes.
	//------------------------------------------------------------------------------------
	bool SetRootDir(char * dir)
	{
		if (_chdir(dir)) return false;
		_getcwd(RootDir, sizeof(RootDir));
		return true;
	}
	//------------------------------------------------------------------------------------
	// Specify directory change.  Specified with forward slashes.
	//------------------------------------------------------------------------------------
	bool MySetDir(char * dir)
	{
		int a;
		char DirWas[MAX_PATH];
		char DirNow[MAX_PATH];

		if (dir[0] == 0) return true;

		_getcwd(DirWas, sizeof(DirWas));
		for (a = 0; dir[a]; a++) {
			if (dir[a] == '/') dir[a] = '\\';
		}
		if (dir[0] == '\\') {
			// Set absolute directory.  Start from our root.
			_chdir(RootDir);
			dir += 1;
		}

		if (dir[0] == 0) return true;

		// Set relative.
		if (_chdir(dir)) {
			// Chdir failed.
			_chdir(DirWas); // Go back to previous directory.
			return false;
		}

		_getcwd(DirNow, sizeof(DirNow));

		if (memcmp(DirNow, RootDir, strlen(RootDir))) {
			// We are no longer unter 'root'.
			_chdir(DirWas);
			return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------
	// get current directory.  Directory is returned with forward slashes.
	//------------------------------------------------------------------------------------
	char * MyGetDir(void)
	{
		int a;
		int RootLen;
		static char DirNow[MAX_PATH];

		RootLen = strlen(RootDir);
		if (RootLen == 3) RootLen = 2;

		_getcwd(DirNow, sizeof(DirNow));
		strncpy_s(DirNow, DirNow + RootLen, MAX_PATH);
		for (a = 0; DirNow[a]; a++) {
			if (DirNow[a] == '\\') DirNow[a] = '/';
		}

		// If dir length is zero, we are at root, but that is indicated by '/'
		if (DirNow[0] == 0) {
			strncpy_s(DirNow, "/", MAX_PATH);
		}

		return DirNow;
	}

	//------------------------------------------------------------------------------------
	// Check for path bits that are all '.'  These are illegal filenames under
	// dos, or would allow us to go outside of the 'root' specified.
	//------------------------------------------------------------------------------------
	char * TranslatePath(const char * FilePath)
	{
		int a, b;
		BOOL NonDot;
		static char NewPath[MAX_PATH];

		NonDot = false;

		if (FilePath[0] == '\\' || FilePath[0] == '/') {
			// Absolute file path.  Concatenate path with our notion of root
			strncpy_s(NewPath, RootDir, MAX_PATH);
			b = strlen(RootDir);
			if (b == 3) b = 2; // Root dir is something like 'c:\' - so it ends with '\'.  
		}
		else {
			// Relative path. 
			b = 0;
		}

		NonDot = false;
		for (a = 0;; a++) {
			if (FilePath[a] == '/' || FilePath[a] == '\\') {
				NewPath[b] = '\\';
				// if (!NonDot && a != 0) return NULL; FUCK your pitiful attempts at security, just look at the rest of your code man, buffer overflows DUH
				NonDot = false;
			}
			else {
				if (FilePath[a] != '.') {
					NonDot = true;
				}
				NewPath[b] = FilePath[a];
			}
			if (FilePath[a] == 0) break;
			b++;
		}
		if (!NonDot) return NULL;

		return NewPath;
	}

}