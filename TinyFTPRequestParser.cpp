#include "TinyFTPRequest.h"
#include "TinyFTPRequestParser.h"

namespace TinyWinFTP
{

	TinyFTPRequestParser::TinyFTPRequestParser()
	{
		std::vector<std::string> FTPCommands;

		FTPCommands.push_back("USER");
		FTPCommands.push_back("PASS");
		FTPCommands.push_back("PWD");
		FTPCommands.push_back("CWD");
		FTPCommands.push_back("LIST");
		FTPCommands.push_back("NLST");
		FTPCommands.push_back("PASV");
		FTPCommands.push_back("RETR");
		FTPCommands.push_back("STOR");
		FTPCommands.push_back("PORT");
		FTPCommands.push_back("TYPE");
		FTPCommands.push_back("MODE");
		FTPCommands.push_back("QUIT");
		FTPCommands.push_back("ABOR");
		FTPCommands.push_back("DELE");
		FTPCommands.push_back("RMD");
		FTPCommands.push_back("XRMD");
		FTPCommands.push_back("MKD");
		FTPCommands.push_back("XMKD");
		FTPCommands.push_back("XPWD");
		FTPCommands.push_back("SYST");
		FTPCommands.push_back("REST");
		FTPCommands.push_back("RNFR");
		FTPCommands.push_back("RNTO");
		FTPCommands.push_back("STAT");
		FTPCommands.push_back("NOOP");
		FTPCommands.push_back("MDTM");
		FTPCommands.push_back("SIZE");
		FTPCommands.push_back("SITE");

		matcher.SetKeywords(FTPCommands);
	}

	void TinyFTPRequestParser::reset()
	{
	}

	TinyFTPRequestParser::ParserResult TinyFTPRequestParser::parse(TinyFTPRequest& req, char*& begin, char*& end)
	{
		TinyFTPRequestParser::ParserResult result;
		if (end - begin < 4) 
		{
			result = NEEDMORE;
			return result;
		}

		char* pLimit = end;
		char* pBeg = begin;
		char* pCur = begin;

		if (pCur[4] == ' ')
			pCur += 4;
		else 
		{
			while (*pCur != ' ' && *pCur != '\r' && pCur != pLimit)
				++pCur;
		}

		if (pCur == pLimit)
			return FAIL;

		size_t commandIndex = 0;
		if (!matcher.MatchSubstring(pBeg, pCur, commandIndex))
			return FAIL;
		else
			req.type = (TinyFTPRequest::FTPRequestType) commandIndex;

		// skip the space
		if (*pCur == ' ')
			++pCur;

		// skip tailing whitespace
		while ((pCur < pLimit) && *(pLimit - 1) == ' ' || *(pLimit - 1) == '\r' || *(pLimit - 1) == '\n')
			--pLimit;

		if (pLimit < pCur)
			result = FAIL;
		else 
		{
			req.param.assign(pCur, pLimit);
			result = SUCCESS;
		}
		return result;
	}

}