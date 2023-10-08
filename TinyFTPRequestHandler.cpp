#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <fstream>
#include <sstream>
#include <string>

#include <stdio.h>  
#include <stdlib.h>  
#include <io.h>  
#include <time.h>  
#include <direct.h>
#include <fcntl.h>
#include <sys\types.h>
#include <sys\stat.h>

#include "TinyFTPRequestHandler.h"
#include "TinyFTPSession.h"
#include "TinyFTPRequest.h"
#include "TinyFTPReply.h"

namespace TinyWinFTP
{
	TinyFTPRequestHandler::TinyFTPRequestHandler()
		: rnFrString(""),
		curMaxPassivePort(PASV_PORT_RANGE_START),
		reusablePassivePorts(8192)
	{
	}

	void TinyFTPRequestHandler::ServiceRetrCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession)
	{
		// File opened succesfully, so make the connection
		asio::write(pSession->getSocket(), asio::buffer(StatusStrings::opening_binary_connection, sizeof(StatusStrings::opening_binary_connection) - 1), asio::transfer_all());

		if (pSession->isPassiveMode())
			pSession->startDataSocketPasv();
		else
			pSession->startDataSocketRemote();

		// TODO: Transfer file
		pSession->startFileTransfer(std::string(filename));
		pSession->dataOpInProgress = true;
	}


	void TinyFTPRequestHandler::ServiceStorCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession)
	{
		// File opened succesfully, so make the connection
		asio::write(pSession->getSocket(), asio::buffer(StatusStrings::opening_binary_connection, sizeof(StatusStrings::opening_binary_connection) - 1), asio::transfer_all());

		if (pSession->isPassiveMode())
			pSession->startDataSocketPasv();
		else
			pSession->startDataSocketRemote();

		// TODO: Transfer file
		pSession->startFileUpload(std::string(filename));
		pSession->dataOpInProgress = true;
	}


	void TinyFTPRequestHandler::ServiceStatCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession)
	{
		struct stat FileStat;
		struct tm tm;
		char RepBuf[50];

		if (stat(filename, &FileStat))
		{
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
			return;
		}

		if (FileStat.st_mode & _S_IFDIR)
		{
			// Its a directory.
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error_not_a_plain_file, sizeof(StatusStrings::error_not_a_plain_file) - 1), asio::transfer_all());
			return;
		}


		switch (req.type)
		{
		case TinyFTPRequest::MDTM:
			localtime_s(&tm, &FileStat.st_mtime);

			snprintf(RepBuf, MAX_REPLY_LEN, "213 %04d%02d%02d%02d%02d%02d\r\n",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			break;

		case TinyFTPRequest::xSIZE:
			snprintf(RepBuf, MAX_REPLY_LEN, "213 %u\r\n", FileStat.st_size);
			break;

		default:
			// Internal screwup!
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
			break;
		}
		rep.content = RepBuf;
	}

	namespace
	{
		const char* numToMonth(WORD month)
		{
			if (month < 1 || month > 12)
				abort();
			static const char* MONTH_NAMES[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
			return MONTH_NAMES[month-1];
		}
	}

	void TinyFTPRequestHandler::ServiceListCommands(char *filename, BOOL Long, BOOL UseCtrlConn, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession)
	{
		char repbuf[MAX_REPLY_LEN];
		BOOL ListAll = FALSE;

		if (!UseCtrlConn)
		{
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::opening_connection, sizeof(StatusStrings::opening_connection) - 1), asio::transfer_all());

			if (pSession->isPassiveMode())
				pSession->startDataSocketPasv();
			else
			{
				pSession->startDataSocketRemote();
			}
		}

		{
			WIN32_FIND_DATAA ffd;
			size_t length_of_arg;
			HANDLE hFind = INVALID_HANDLE_VALUE;
			DWORD dwError = 0;

			length_of_arg = strnlen_s(filename, TinyFTPSession::MAX_PATH_32K);
			if (length_of_arg > (TinyFTPSession::MAX_PATH_32K - 3))
				abort();

			strncat_s(filename, TinyFTPSession::MAX_PATH_32K, "\\*", strlen("\\*"));
			// Find the first file in the directory.
			hFind = FindFirstFileA(filename, &ffd);

			if (INVALID_HANDLE_VALUE == hFind)
				return;

			do
			{
				if (!strncmp(ffd.cFileName, ".", TinyFTPSession::MAX_PATH_32K) || !strncmp(ffd.cFileName, "..", TinyFTPSession::MAX_PATH_32K))
					continue;

				if (Long)
				{
					char DirAttr;
					char WriteAttr;

					SYSTEMTIME stUTC, stLocal;
					// Convert the last-write time to local time.
					FileTimeToSystemTime(&ffd.ftLastWriteTime, &stUTC);
					SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

					char timeStringBuf[128];

					// Build a string showing the date and time.
					snprintf(timeStringBuf, 128, "%s %02d  %04d", numToMonth(stLocal.wMonth), stLocal.wDay, stLocal.wYear);

					DirAttr = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'd' : '-';
					WriteAttr = (ffd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';

					uint64_t fileSize = ((uint64_t)ffd.nFileSizeHigh << 32) + ffd.nFileSizeLow;
					snprintf(repbuf, MAX_REPLY_LEN, "%cr%c-r%c-r%c-   1 root  root    %7llu %s %s\r\n",
						DirAttr, WriteAttr, WriteAttr, WriteAttr,
						fileSize,
						timeStringBuf,
						ffd.cFileName);
				}
				else
				{
					snprintf(repbuf, MAX_REPLY_LEN, "%s\r\n", ffd.cFileName);
				}
				rep.content += std::string(repbuf);
			} while (FindNextFileA(hFind, &ffd) != 0);
			FindClose(hFind);
		}

		if (!UseCtrlConn)
		{
			if (!rep.content.empty())
			{
				asio::write(pSession->getDataSocket(), asio::buffer(rep.content), asio::transfer_all());
				rep.content.clear();
			}
			pSession->closeDataSocket();
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::transfer_complete, sizeof(StatusStrings::transfer_complete) - 1), asio::transfer_all());
		}
	}

	void TinyFTPRequestHandler::handleRequest(const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession)
	{
		rep.content.clear();
		char buf[TinyFTPSession::MAX_PATH_32K];
		strncpy_s(buf, req.param.c_str(), TinyFTPSession::MAX_PATH_32K);
		char repbuf[MAX_REPLY_LEN];
		int pasvPort;
		char * NewPath;

		switch (req.type)
		{
		case TinyFTPRequest::USER:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::login_accepted, sizeof(StatusStrings::login_accepted) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::PASS:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::user_logged_in, sizeof(StatusStrings::user_logged_in) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::SYST:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::syst_string, sizeof(StatusStrings::syst_string) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::PASV:
			pasvPort = acquirePassivePort();
			pSession->setPasvPort(pasvPort);
			ourAddrString = pSession->getSocket().local_endpoint().address().to_string();
			snprintf(repbuf, MAX_REPLY_LEN, "227 Entering Passive Mode (%s,%d,%d).\r\n",
				ourAddrString.c_str(), pasvPort >> 8, pasvPort & 0xff);
			for (int a = 0; a < 50; a++)
			{
				if (repbuf[a] == 0) break;
				if (repbuf[a] == '.') repbuf[a] = ',';
			}
			rep.content = std::string(repbuf);
			break;

		case TinyFTPRequest::XPWD:
		case TinyFTPRequest::PWD: // Print working directory 
		{
			std::string sessionCurDir = pSession->getCurDir();
			std::replace(sessionCurDir.begin(), sessionCurDir.end(), '\\', '/');
			if (!sessionCurDir.empty()) // Some clients hate getting empty dir name in 257 FTP response, send them root when curDir is empty
				snprintf(repbuf, MAX_REPLY_LEN, "257 \"%s\"\r\n", sessionCurDir.c_str());
			else
				snprintf(repbuf, MAX_REPLY_LEN, "257 \"/\"\r\n");
			rep.content = std::string(repbuf);
		}
		break;

		case TinyFTPRequest::NLST: // Request directory, names only.
			if (!strncmp(buf, "-la", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-l", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-a", TinyFTPSession::MAX_PATH_32K))
				strncpy_s(buf, "", TinyFTPSession::MAX_PATH_32K);
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			ServiceListCommands(NewPath, FALSE, FALSE, req, rep, pSession);
			break;

		case TinyFTPRequest::LIST: // Request directory, long version.
			if (!strncmp(buf, "-la", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-l", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-a", TinyFTPSession::MAX_PATH_32K))
				strncpy_s(buf, "", TinyFTPSession::MAX_PATH_32K);
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
			}
			ServiceListCommands(NewPath, TRUE, FALSE, req, rep, pSession);
			break;

		case TinyFTPRequest::STAT: // Just like LIST, but use control connection.
			if (!strncmp(buf, "-la", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-l", TinyFTPSession::MAX_PATH_32K) || !strncmp(buf, "-a", TinyFTPSession::MAX_PATH_32K))
				strncpy_s(buf, "", TinyFTPSession::MAX_PATH_32K);
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			ServiceListCommands(NewPath, TRUE, TRUE, req, rep, pSession);
			break;

		case TinyFTPRequest::DELE:
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			if (!DeleteFileA(NewPath))
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
			else
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::delete_successful, sizeof(StatusStrings::delete_successful) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::RMD:
		case TinyFTPRequest::MKD:
		case TinyFTPRequest::XMKD:
		case TinyFTPRequest::XRMD:
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			if (req.type == TinyFTPRequest::MKD || req.type == TinyFTPRequest::XMKD) {
				if (_mkdir(NewPath))
					asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
				else
					asio::write(pSession->getSocket(), asio::buffer(StatusStrings::dir_created, sizeof(StatusStrings::dir_created) - 1), asio::transfer_all());
			}
			else
			{
				if (_rmdir(NewPath))
					asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
				else
					asio::write(pSession->getSocket(), asio::buffer(StatusStrings::dir_removed, sizeof(StatusStrings::dir_removed) - 1), asio::transfer_all());
			}
			break;

		case TinyFTPRequest::RNFR:
			NewPath = pSession->translatePath(buf);
			if (NewPath)
			{
				strncpy_s(repbuf, NewPath, MAX_REPLY_LEN);
				rnFrString = repbuf;
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::file_exists, sizeof(StatusStrings::file_exists) - 1), asio::transfer_all());
			}
			else
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::RNTO:
			// Must be immediately preceeded by RNFR!
			NewPath = pSession->translatePath(buf);
			if (rename(rnFrString.c_str(), NewPath))
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::error, sizeof(StatusStrings::error) - 1), asio::transfer_all());
			else
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::rnto_successful, sizeof(StatusStrings::rnto_successful) - 1), asio::transfer_all());
			rnFrString.clear();
			break;

		case TinyFTPRequest::ABOR:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::aborted, sizeof(StatusStrings::aborted) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::xSIZE:
		case TinyFTPRequest::MDTM:
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			ServiceStatCommand(NewPath, req, rep, pSession);
			break;

		case TinyFTPRequest::CWD: // Change working directory
			if (!pSession->setCurDir(buf))
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::cwd_failed, sizeof(StatusStrings::cwd_failed) - 1), asio::transfer_all());
			else
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::cwd_successful, sizeof(StatusStrings::cwd_successful) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::TYPE: // Accept file TYPE commands, but ignore.
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::type_successful, sizeof(StatusStrings::type_successful) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::NOOP:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::ok, sizeof(StatusStrings::ok) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::ALLO:
		case TinyFTPRequest::sALLO: 
			pSession->setAlloSize(atoi(req.param.c_str()));
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::ok, sizeof(StatusStrings::ok) - 1), asio::transfer_all());
			break;


		case TinyFTPRequest::PORT: // Set the TCP/IP addres for trasnfers.
		{
			pSession->setPortString(req.param);
		}
		asio::write(pSession->getSocket(), asio::buffer(StatusStrings::port_successful, sizeof(StatusStrings::port_successful) - 1), asio::transfer_all());
		break;

		case TinyFTPRequest::RETR: // Retrieve File and send it
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			ServiceRetrCommand(NewPath, req, rep, pSession);
			break;

		case TinyFTPRequest::STOR: // Store the file.
			NewPath = pSession->translatePath(buf);
			if (NewPath == NULL)
			{
				asio::write(pSession->getSocket(), asio::buffer(StatusStrings::path_perm_error, sizeof(StatusStrings::path_perm_error) - 1), asio::transfer_all());
				break;
			}
			ServiceStorCommand(NewPath, req, rep, pSession);
			break;

		case TinyFTPRequest::UNKNOWN_COMMAND:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::unknown_command, sizeof(StatusStrings::unknown_command) - 1), asio::transfer_all());
			break;

		case TinyFTPRequest::QUIT:
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::bye, sizeof(StatusStrings::bye) - 1), asio::transfer_all());
			// TODO: close session and free passv port

		case TinyFTPRequest::FEAT:
		case TinyFTPRequest::OPTS:
		case TinyFTPRequest::sFEAT:
		case TinyFTPRequest::sOPTS:
		case TinyFTPRequest::sSYST:
		case TinyFTPRequest::sSITE:
		case TinyFTPRequest::SITE:
		default: // Any command not implemented, return not recognized response.
			asio::write(pSession->getSocket(), asio::buffer(StatusStrings::unknown_command, sizeof(StatusStrings::unknown_command) - 1), asio::transfer_all());
			rep.content.clear();
			break;
		}
	}

	int TinyFTPRequestHandler::acquirePassivePort()
	{
		int nextPasvPort = 0;
		if (!reusablePassivePorts.pop(nextPasvPort))
			nextPasvPort = curMaxPassivePort++;

		return nextPasvPort;
	}
	void TinyFTPRequestHandler::releasePassivePort(int pasvPort)
	{
		reusablePassivePorts.push(pasvPort);
	}

}