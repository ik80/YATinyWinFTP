#include <functional>

#include <asio\io_service.hpp>
#include <asio\placeholders.hpp>
#include <asio\error.hpp>
#include <asio\ip\tcp.hpp>
#include <asio\buffer.hpp>

#include "TinyFTPSession.h"

namespace TinyWinFTP
{

	namespace 
	{
		uint16_t atous(const char* beg, const char* end)
		{
			int value = 0;
			int len = end - beg; // assuming end is past beg;
			switch (len)
				// handle up to 5 digits, assume we're 16-bit
			{
			case 5:
				value += (beg[len - 5] - '0') * 10000;
			case 4:
				value += (beg[len - 4] - '0') * 1000;
			case 3:
				value += (beg[len - 3] - '0') * 100;
			case 2:
				value += (beg[len - 2] - '0') * 10;
			case 1:
				value += (beg[len - 1] - '0');
			}
			return value;
		}
	}

	template <typename Handler>
	void transmit_file(asio::ip::tcp::socket& socket, asio::windows::random_access_handle& file, Handler handler, uint64_t offset, LARGE_INTEGER total)
	{
		asio::windows::overlapped_ptr overlapped(socket.get_io_service(), handler);

		uint64_t totalBytes = total.HighPart;
		totalBytes = totalBytes << 32;
		totalBytes += total.LowPart;

		uint64_t bytesToWriteLarge = std::min(TinyFTPSession::TRANSMIT_FILE_LIMIT, totalBytes - offset);
		DWORD bytesToWrite = (DWORD) bytesToWriteLarge;
		OVERLAPPED* realOverlapped = overlapped.get();

		DWORD offsetHigh = offset >> 32;
		DWORD offsetLow = (DWORD) (offset - ((uint64_t)offsetHigh << 32));

		realOverlapped->Offset = offsetLow;
		realOverlapped->OffsetHigh = offsetHigh;
		BOOL ok = ::TransmitFile(socket.native_handle(), file.native_handle(), bytesToWrite, 0, realOverlapped, 0, 0);
		DWORD last_error = ::GetLastError();

		// Check if the operation completed immediately.
		if (!ok && last_error != ERROR_IO_PENDING)
		{
			// The operation completed immediately, so a completion notification needs
			// to be posted. When complete() is called, ownership of the OVERLAPPED-
			// derived object passes to the io_service.
			asio::error_code ec(last_error,
				asio::error::get_system_category());
			overlapped.complete(ec, 0);
		}
		else
		{
			// The operation was successfully initiated, so ownership of the
			// OVERLAPPED-derived object has passed to the io_service.
			overlapped.release();
		}
	}

	TinyFTPSession::TinyFTPSession(asio::io_service& in_ioService, asio::ip::tcp::socket&& in_socket,
		TinyFTPRequestHandler& handler, TinyFTPRequestParser& parser)
		: service(in_ioService),
		socket(std::move(in_socket)),
		requestHandler(handler),
		requestParser(parser),
		fileBytesSent(0),
		pasvPort(-1),
		fileToSend(in_ioService)
	{
		fileBytesTotal.QuadPart = 0;
		dataOpInProgress = false;
		dataSocketConnected = false;
	}

	TinyFTPSession::~TinyFTPSession() 
	{
		asio::error_code ignored_ec;
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		if (socketData.get())
			socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		if (fileToSend.is_open())
			fileToSend.close();
		fileBytesTotal.QuadPart = 0;
		fileBytesSent = 0;
	}

	void TinyFTPSession::start()
	{
		asio::async_write(socket, asio::buffer(WELCOME_STRING, strlen(WELCOME_STRING)),
			std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(),
				std::placeholders::_1));
	}

	void TinyFTPSession::handleReadControl(const asio::error_code& e, std::size_t bytes_transferred)
	{
		if (!e)
		{
			char * beginBuffer = buffer.data(), *endBuffer = buffer.data() + bytes_transferred;
			TinyFTPRequestParser::ParserResult result = requestParser.parse(request, beginBuffer, endBuffer);

			if (result == TinyFTPRequestParser::SUCCESS)
			{
				requestHandler.handleRequest(request, reply, this);
				if (!reply.content.empty())
					asio::async_write(socket, asio::buffer(reply.content.data(), reply.content.size()), std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(), std::placeholders::_1));
				else if (!dataOpInProgress)
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
			}
			else if (result == TinyFTPRequestParser::FAIL)
			{
				reply.content = StatusStrings::bad_request;
				asio::async_write(socket, asio::buffer(reply.content.data(), reply.content.size()), std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(), std::placeholders::_1));
			}
			else if (result == TinyFTPRequestParser::NEEDMORE) 
				socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
		}
		else
		{
			// Initiate graceful TinyFTPSession closure.
			asio::error_code ignored_ec;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (socketData.get())
				socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}
	}

	// for STOR command
	void TinyFTPSession::handleReadData(const asio::error_code& e, std::size_t bytes_transferred)
	{
		if (!e)
		{
			std::lock_guard<std::mutex> buffersGuard(uploadBuffers.queueMutex);
			uploadBuffers.fullBuffers.emplace_back(bytes_transferred,uploadBuffers.curNetworkBuffer);
			uploadBuffers.curNetworkBuffer = 0;

			if (uploadBuffers.emptyBuffers.empty()) 
			{
				uploadBuffers.starved = true;
			}
			else 
			{
				uploadBuffers.starved = false;

				// TODO: Fix this pile of crap
				// thing is with receiving files ftp server doesnt know the size beforehand. this doesnt play way AT ALL with asio / async.
				// client should close the connection for the file when done uploading, but that never happens either
				// SOoo... hacks below: after every successful read try to select socket for 100ms, if not - treat it as timeout, otherwise - try
				// peeking on the amount of data available on socket, if its 0 then remote has closed the connection and thats where "done" flag is set
				// all this for the sole wish not to block in async handler...
				fd_set readSet;
				readSet.fd_count = 1;
				readSet.fd_array[0] = socketData->native_handle();
				timeval  tv;
				tv.tv_sec = 0;
				tv.tv_usec = 100000000;
				int selectRes = ::select(1, &readSet, 0, 0, &tv);

				if (selectRes) 
				{

					unsigned long bytes_available;
					ioctlsocket(socketData->native_handle(), FIONREAD, &bytes_available);
					if (bytes_available) 
					{
						auto buffersFront = std::move(uploadBuffers.emptyBuffers.front());
						uploadBuffers.curNetworkBuffer = buffersFront.second;
						uploadBuffers.emptyBuffers.pop_front();
						socketData->async_read_some(asio::buffer(uploadBuffers.curNetworkBuffer->data(), uploadBuffers.curNetworkBuffer->max_size()), std::bind(&TinyFTPSession::handleReadData, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
					}
					else 
					{
						uploadBuffers.noMoreReads = true;
					}
				}
				else 
				{
					asio::error_code ignored_ec;
					socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
					if (socketData.get())
						socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
					if (fileToSend.is_open())
						fileToSend.close();
					fileBytesTotal.QuadPart = 0;
					fileBytesSent = 0;
				}
			}

			if (!uploadBuffers.writeInProgress) 
			{
				uploadBuffers.writeInProgress = true;
				auto buffersFront = std::move(uploadBuffers.fullBuffers.front());
				uploadBuffers.curDiskBuffer = buffersFront.second;
				size_t bytesToWrite = buffersFront.first;
				uploadBuffers.fullBuffers.pop_front();

				GetFileSizeEx(fileToSend.native_handle(), &fileBytesTotal);
				uint64_t totalBytes = fileBytesTotal.HighPart;
				totalBytes = totalBytes << 32;
				totalBytes += fileBytesTotal.LowPart;

				fileToSend.async_write_some_at(totalBytes, asio::buffer(uploadBuffers.curDiskBuffer->data(), bytesToWrite), std::bind(&TinyFTPSession::handleWriteDisk, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
			}
		}
		else
		{
			// Initiate graceful TinyFTPSession closure.
			asio::error_code ignored_ec;
			if (socketData.get())
				socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		}
	}

	void TinyFTPSession::handleWriteDisk(const asio::error_code& e, std::size_t bytes_transferred)
	{
		if (!e)
		{
			std::lock_guard<std::mutex> buffersGuard(uploadBuffers.queueMutex);

			uploadBuffers.emptyBuffers.emplace_back(0,uploadBuffers.curDiskBuffer);
			uploadBuffers.curDiskBuffer = 0;

			if (uploadBuffers.starved)
			{
				uploadBuffers.starved = false;
				auto buffersFront = std::move(uploadBuffers.emptyBuffers.front());
				uploadBuffers.curNetworkBuffer = buffersFront.second;
				uploadBuffers.emptyBuffers.pop_front();
				socketData->async_read_some(asio::buffer(uploadBuffers.curNetworkBuffer->data(), uploadBuffers.curNetworkBuffer->max_size()), std::bind(&TinyFTPSession::handleReadData, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
			}

			if (uploadBuffers.fullBuffers.empty())
			{
				uploadBuffers.writeInProgress = false;
				if (uploadBuffers.noMoreReads) 
				{
					uploadBuffers.noMoreReads = false;
					uploadBuffers.starved = false;
					dataOpInProgress = false;
					closeDataSocket();
					if (fileToSend.is_open())
						fileToSend.close();
					asio::write(getSocket(), asio::buffer(StatusStrings::transfer_complete, sizeof(StatusStrings::transfer_complete) - 1), asio::transfer_all());
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
				}
			}
			else 
			{
				auto buffersFront = std::move(uploadBuffers.fullBuffers.front());
				uploadBuffers.curDiskBuffer = buffersFront.second;
				size_t bytesToWrite = buffersFront.first;
				uploadBuffers.fullBuffers.pop_front();

				GetFileSizeEx(fileToSend.native_handle(), &fileBytesTotal);
				uint64_t totalBytes = fileBytesTotal.HighPart;
				totalBytes = totalBytes << 32;
				totalBytes += fileBytesTotal.LowPart;

				fileToSend.async_write_some_at(totalBytes, asio::buffer(uploadBuffers.curDiskBuffer->data(), bytesToWrite), std::bind(&TinyFTPSession::handleWriteDisk, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
			}
		}
		else
		{
			// Initiate graceful TinyFTPSession closure.
			asio::error_code ignored_ec;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (socketData.get())
				socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (fileToSend.is_open())
				fileToSend.close();
			fileBytesTotal.QuadPart = 0;
			fileBytesSent = 0;
		}
	}

	void TinyFTPSession::handleWriteControl(const asio::error_code& e)
	{
		if (!e)
		{
			if (!dataOpInProgress)
			{
				socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()),
					std::bind(&TinyFTPSession::handleReadControl, shared_from_this(),
						std::placeholders::_1,
						std::placeholders::_2));
			}
		}
		else 
		{
			// Initiate graceful TinyFTPSession closure.
			asio::error_code ignored_ec;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (socketData.get())
				socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (fileToSend.is_open())
				fileToSend.close();
			fileBytesTotal.QuadPart = 0;
			fileBytesSent = 0;
		}
	}

	void TinyFTPSession::handleWriteData(const asio::error_code& e)
	{
		if (!e)
		{
			bool expectedState = true;
			if (dataOpInProgress.compare_exchange_strong(expectedState, false) == true)
			{
				uint64_t fileBytesTotalBytes = fileBytesTotal.HighPart;
				fileBytesTotalBytes = fileBytesTotalBytes << 32;
				fileBytesTotalBytes += fileBytesTotal.LowPart;
				if (fileBytesTotalBytes - fileBytesSent <= TRANSMIT_FILE_LIMIT)
				{
					fileBytesTotal.QuadPart = 0;
					fileBytesSent = 0;
					closeDataSocket();
					if (fileToSend.is_open())
						fileToSend.close();

					asio::write(getSocket(), asio::buffer(StatusStrings::transfer_complete, sizeof(StatusStrings::transfer_complete) - 1), asio::transfer_all());
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
				}
				else 
				{
					dataOpInProgress = true; // race! race here!
					fileBytesSent += TRANSMIT_FILE_LIMIT;
					transmit_file(*socketData, fileToSend, std::bind(&TinyFTPSession::handleWriteData, shared_from_this(), std::placeholders::_1), fileBytesSent, fileBytesTotal);
				}
			}
			socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()),
				std::bind(&TinyFTPSession::handleReadControl, shared_from_this(),
					std::placeholders::_1,
					std::placeholders::_2));
		}
		else
		{
			// Initiate graceful TinyFTPSession closure.
			asio::error_code ignored_ec;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (socketData.get())
				socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
			if (fileToSend.is_open())
				fileToSend.close();
			fileBytesTotal.QuadPart = 0;
			fileBytesSent = 0;
		}
	}

	// sets pasv port to use for this connection
	void TinyFTPSession::setPasvPort(int port) 
	{
		pasvPort = port;
		tcpAcceptor.reset(new asio::ip::tcp::acceptor(service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), pasvPort)));
	}

	// starts data socket up in remote mode
	void TinyFTPSession::startDataSocketRemote()
	{
		unsigned long addr;
		unsigned short port;

		char *b = (char*)portString.c_str(), *e = b;
		while (*e != ',') ++e;
		addr = atous(b, e);
		b = e + 1;
		e = b;
		while (*e != ',') ++e;
		addr = addr*256 + atous(b, e);
		b = e + 1;
		e = b;
		while (*e != ',') ++e;
		addr = addr * 256 + atous(b, e);
		b = e + 1;
		e = b;
		while (*e != ',') ++e;
		addr = addr * 256 + atous(b, e);
		b = e + 1;
		e = b;
		while (*e != ',') ++e;
		port = atous(b, e);
		b = e + 1;
		e = b;
		while (*e != 0) ++e;
		port = port * 256 + atous(b, e);

		socketData.reset(new asio::ip::tcp::socket(service));
		asio::ip::tcp::endpoint remoteEndpoint(asio::ip::address(asio::ip::address_v4(addr)), port);
		socketData->connect(remoteEndpoint);
		socketData->set_option(asio::ip::tcp::no_delay(false));
		dataSocketConnected = true;
	}

	// starts data socket up in pasv mode
	void TinyFTPSession::startDataSocketPasv() 
	{
		socketData.reset(new asio::ip::tcp::socket(service));
		tcpAcceptor->accept(*socketData);
		socketData->set_option(asio::ip::tcp::no_delay(false));
		dataSocketConnected = true;
	}

	// close data socket
	void TinyFTPSession::closeDataSocket() 
	{
		socketData->close();
		socketData.reset();
		if (pasvPort != -1)
		{
			requestHandler.releasePassivePort(pasvPort);
			pasvPort = -1;
		}
	}

	// is session in passive mode
	bool TinyFTPSession::isPassiveMode() 
	{
		if (pasvPort != -1)
			return true;
		return false;
	}

	void TinyFTPSession::startFileTransfer(std::string filename_)
	{
		asio::error_code ec;
		fileToSend.assign(::CreateFileA(filename_.c_str(), GENERIC_READ, 0, 0,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0), ec);
		GetFileSizeEx(fileToSend.native_handle(), &fileBytesTotal);
		if (fileToSend.is_open())
		{
			transmit_file(*socketData, fileToSend, std::bind(&TinyFTPSession::handleWriteData, shared_from_this(), std::placeholders::_1), 0, fileBytesTotal);
		}
	}

	void TinyFTPSession::startFileUpload(std::string filename_)
	{
		if (fileToSend.is_open())
			fileToSend.close();
		fileBytesTotal.QuadPart = 0;
		fileBytesSent = 0;

		if (filename_.find("\\.\\") != std::string::npos)
			filename_.replace(filename_.find("\\.\\"), strlen("\\.\\"), "\\");
		asio::error_code ec;
		fileToSend.assign(::CreateFileA(filename_.c_str(), GENERIC_WRITE, 0, 0,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0), ec);
		if (fileToSend.is_open())
		{
			std::lock_guard<std::mutex> buffersGuard(uploadBuffers.queueMutex);

			if (!uploadBuffers.isInitialized)
				uploadBuffers.init();

			auto buffersFront = std::move(uploadBuffers.emptyBuffers.front());
			uploadBuffers.curNetworkBuffer = buffersFront.second;
			uploadBuffers.emptyBuffers.pop_front();

			socketData->async_read_some(asio::buffer(uploadBuffers.curNetworkBuffer->data(), uploadBuffers.curNetworkBuffer->max_size()), std::bind(&TinyFTPSession::handleReadData, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
		}
	}

	std::string TinyFTPSession::getPortString()
	{
		return portString;
	}

	void TinyFTPSession::setPortString(std::string newPortString)
	{
		portString = newPortString;
	}

	TinyFTPUploadBuffers::TinyFTPUploadBuffers() : isInitialized(false), starved(false), writeInProgress(false), noMoreReads(false)
	{
	}

	TinyFTPUploadBuffers::~TinyFTPUploadBuffers() 
	{
		while (!emptyBuffers.empty())
		{
			std::pair<size_t, std::array<char, RECV_BUFFER_SIZE> * > pBuffer = emptyBuffers.front();
			emptyBuffers.pop_front();
			delete pBuffer.second;
		} 
		while (!fullBuffers.empty())
		{
			std::pair<size_t, std::array<char, RECV_BUFFER_SIZE> *> pBuffer = fullBuffers.front();
			fullBuffers.pop_front();
			delete pBuffer.second;
		} 
	}

	void TinyFTPUploadBuffers::init() 
	{
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		isInitialized = true;
	}
}