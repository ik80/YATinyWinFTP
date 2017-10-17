#ifndef IK80_TINYFTPREQUESTHANDLER_H_
#define IK80_TINYFTPREQUESTHANDLER_H_

#include <string>

#include "LFMPMCQueue.h"

#include "TinyFTPReply.h"
#include "TinyFTPRequest.h"


namespace TinyWinFTP
{
	class TinyFTPSession;

	/// The common handler for all incoming requests.
	class TinyFTPRequestHandler
	{
		static const size_t PASV_PORT_RANGE_START = 50000;
		static const size_t MAX_REPLY_LEN = 4096;
	public:
		/// Construct with a directory containing files to be served.
		explicit TinyFTPRequestHandler(const std::string& inDocRoot);

		/// Handle a request and produce a reply.
		void handleRequest(const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession);

		int acquirePassivePort();
		void releasePassivePort(int pasvPort);

	private:
		/// The directory containing the files to be served.
		std::string docRoot;
		std::string ourAddrString;
		std::string rnFrString;

		int curMaxPassivePort;
		LFMPMCQueue<int> reusablePassivePorts;


		void ServiceStorCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession);
		void ServiceRetrCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession);
		void ServiceListCommands(char *filename, BOOL Long, BOOL UseCtrlConn, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession);
		void ServiceStatCommand(char *filename, const TinyFTPRequest& req, TinyFTPReply& rep, TinyFTPSession* pSession);

		TinyFTPRequestHandler(const TinyFTPRequestHandler& other) = delete;
		TinyFTPRequestHandler(TinyFTPRequestHandler&& other) = delete;
	};

}
#endif // IK80_TINYFTPREQUESTHANDLER_H_
