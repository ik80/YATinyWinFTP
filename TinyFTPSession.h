#ifndef IK80_TINYFTPSESSION_H_
#define IK80_TINYFTPSESSION_H_

#include <memory>
#include <array>
#include <atomic>
#include <mutex>

#include "TinyFTPRequestHandler.h"
#include "TinyFTPRequestParser.h"

namespace TinyWinFTP
{

	static const size_t RECV_BUFFER_SIZE = 256*1024;

	struct TinyFTPUploadBuffers 
	{
		TinyFTPUploadBuffers();
		~TinyFTPUploadBuffers();
		void init();

		bool isInitialized;
		bool starved;
		bool writeInProgress;
		bool noMoreReads;
		std::mutex queueMutex;

		/// Buffer for incoming commands.
		std::list< std::pair<size_t, std::array<char, RECV_BUFFER_SIZE> * > > emptyBuffers;
		std::list< std::pair<size_t, std::array<char, RECV_BUFFER_SIZE> * > > fullBuffers;

		std::array<char, RECV_BUFFER_SIZE> * curNetworkBuffer;
		std::array<char, RECV_BUFFER_SIZE> * curDiskBuffer;

		TinyFTPUploadBuffers(const TinyFTPUploadBuffers& other) = delete;

	};

	/// Represents a single TinyFTPSession from a client.
	class TinyFTPSession
		: public std::enable_shared_from_this<TinyFTPSession>
	{
		static constexpr const char * WELCOME_STRING = "220 TinyWinFTP ready\n";
	public:
		static const size_t MAX_COMMAND_LEN = 384;
		static const uint64_t TRANSMIT_FILE_LIMIT = 1024*1024*1024;
		static const size_t MAX_PATH_32K = 32768;

		/// Construct a TinyFTPSession with the given io_service.
		explicit TinyFTPSession(asio::io_service& io_service, asio::ip::tcp::socket&& socket, TinyFTPRequestHandler& handler, TinyFTPRequestParser& parser, std::string docRoot);

		/// closes the socket
		~TinyFTPSession();

		/// Get the control socket associated with the TinyFTPSession.
		inline asio::ip::tcp::socket& TinyFTPSession::getSocket()
		{
			return socket;
		}

		/// Get the data socket associated with the TinyFTPSession.
		inline asio::ip::tcp::socket& TinyFTPSession::getDataSocket()
		{
			return *socketData;
		}

		// sets pasv port to use for this connection
		void setPasvPort(int port);

		// starts data socket up
		void startDataSocketRemote();

		// starts data socket up
		void startDataSocketPasv();

		// close data socket
		void closeDataSocket();

		// is session in passive mode
		bool isPassiveMode();

		// remote address
		std::string getPortString();
		void setPortString(std::string newPortString);

		/// Start the first asynchronous operation for the TinyFTPSession.
		void start();
		void startFileTransfer(std::string filename_);
		void startFileUpload(std::string filename_);

		// is data op in progress
		std::atomic_bool dataOpInProgress;

		bool setCurDir(char * in_szNewCurDir);
		const char * getCurDir();
		char * translatePath(char * buffer);

	private:
		/// Handle completion of a control read operation.
		void handleReadControl(const asio::error_code& e, std::size_t bytes_transferred);

		/// Handle completion of a data read operation.
		void handleReadData(const asio::error_code& e, std::size_t bytes_transferred);
		void handleWriteDisk(const asio::error_code& e, std::size_t bytes_transferred);

		/// Handle completion of a control socket write operation.
		void handleWriteControl(const asio::error_code& e);

		/// Handle completion of a data socket write operation.
		void handleWriteData(const asio::error_code& e);

		/// Socket for the command connection.
		asio::ip::tcp::socket socket;

		/// Socket for the data connection.
		std::unique_ptr<asio::ip::tcp::socket> socketData;

		/// Relevant IO service
		asio::io_service& service;

		/// The handler used to process the incoming request.
		TinyFTPRequestHandler& requestHandler;

		/// Buffer for incoming commands.
		std::array<char, MAX_COMMAND_LEN> buffer;

		TinyFTPUploadBuffers uploadBuffers;

		/// The incoming request.
		TinyFTPRequest request;

		/// The reference to parser for the incoming request.
		TinyFTPRequestParser& requestParser;

		/// The reply to be sent back to the client.
		TinyFTPReply reply;

		asio::windows::random_access_handle fileToSend;
		uint64_t fileBytesSent;
		LARGE_INTEGER fileBytesTotal;

		// is data op in progress
		std::atomic_bool dataSocketConnected;

		std::atomic_int pasvPort;

		// acceptor and listener socket
		std::unique_ptr<asio::ip::tcp::acceptor> tcpAcceptor;

		// remote address
		std::string portString;

		// remote address
		std::string curDirectory;
		std::string docRoot;

		TinyFTPSession(const TinyFTPSession & other) = delete;
		TinyFTPSession(TinyFTPSession && other) = delete;
	};

	using TinyFTPSessionPtr = std::shared_ptr<TinyFTPSession> ;


}

#endif // IK80_TINYFTPSESSION_H_