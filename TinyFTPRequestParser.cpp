#include "TinyFTPRequest.h"
#include "TinyFTPRequestParser.h"

namespace TinyWinFTP
{

	TinyFTPRequestParser::TinyFTPRequestParser()
	{
		commands["USER"] = 0;
		commands["PASS"] = 1;
		commands["PWD"] = 2;
		commands["CWD"] = 3;
		commands["LIST"] = 4;
		commands["NLST"] = 5;
		commands["PASV"] = 6;
		commands["RETR"] = 7;
		commands["STOR"] = 8;
		commands["PORT"] = 9;
		commands["TYPE"] = 10;
		commands["MODE"] = 11;
		commands["QUIT"] = 12;
		commands["ABOR"] = 13;
		commands["DELE"] = 14;
		commands["RMD"] = 15;
		commands["XRMD"] = 16;
		commands["MKD"] = 17;
		commands["XMKD"] = 18;
		commands["XPWD"] = 19;
		commands["SYST"] = 20;
		commands["REST"] = 21;
		commands["RNFR"] = 22;
		commands["RNTO"] = 23;
		commands["STAT"] = 24;
		commands["NOOP"] = 25;
		commands["MDTM"] = 26;
		commands["SIZE"] = 27;
		commands["SITE"] = 28;
		commands["FEAT"] = 29;
		commands["OPTS"] = 30;
		commands["feat"] = 31;
		commands["opts"] = 32;
		commands["syst"] = 33;
		commands["site"] = 34;
		commands["noop"] = 35;
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
		auto it = commands.find(std::string(pBeg, pCur));
		if (it == commands.end())
			return FAIL;
		else
			req.type = (TinyFTPRequest::FTPRequestType) it->second;

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