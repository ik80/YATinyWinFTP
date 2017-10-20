#ifndef IK80_TINYFTPREQUEST_H_
#define IK80_TINYFTPREQUEST_H_

#include <string>
#include <vector>

namespace TinyWinFTP
{
	/// A request received from a client.
	struct TinyFTPRequest
	{
		// FTP Command tokens
		enum FTPRequestType
		{
			USER = 0,
			PASS,
			PWD,
			CWD,
			LIST,
			NLST,
			PASV,
			RETR,
			STOR,
			PORT,
			TYPE,
			MODE,
			QUIT,
			ABOR,
			DELE,
			RMD,
			XRMD,
			MKD,
			XMKD,
			XPWD,
			SYST,
			REST,
			RNFR,
			RNTO,
			STAT,
			NOOP,
			MDTM,
			xSIZE,
			SITE,
			FEAT,
			OPTS,
			sFEAT,
			sOPTS,
			sSYST,
			sSITE,
			UNKNOWN_COMMAND
		};

		FTPRequestType type;
		std::string param;
	};
}
#endif // IK80_TINYFTPREQUEST_H_