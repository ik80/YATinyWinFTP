#include <functional>

#include <iostream>
#include <regex>

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
		asio::windows::overlapped_ptr overlapped(socket.get_executor(), handler);

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

		std::cout << "::TransmitFile, bytes " << bytesToWrite << ", offset " << offset <<  std::endl;

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

	TinyFTPSession::TinyFTPSession(asio::io_service& in_ioService, asio::ip::tcp::socket&& in_socket, TinyFTPRequestHandler& handler, TinyFTPRequestParser& parser, std::string in_docRoot)
		: service(in_ioService),
		socket(std::move(in_socket)),
		requestHandler(handler),
		requestParser(parser),
		fileBytesSent(0),
		pasvPort(-1),
		fileToSend(in_ioService)
	{
		docRoot = in_docRoot;
		std::replace(docRoot.begin(), docRoot.end(), '/', '\\');
		while (*docRoot.rbegin() == '\\' && docRoot.size() > 1)
			docRoot = docRoot.substr(0,docRoot.size() - 1);
		curDirectory = "\\";

		fileBytesTotal.QuadPart = 0;
		dataOpInProgress = false;
		dataSocketConnected = false;
		std::cout << "Session created" << std::endl;
	}

	TinyFTPSession::~TinyFTPSession() 
	{
		asio::error_code ignored_ec;
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);

		if (socketData.get())
			socketData->shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);
		if (fileToSend.is_open())
			fileToSend.close();

		if (tcpAcceptor.get())
		{
			tcpAcceptor->cancel();
			tcpAcceptor->close();
			tcpAcceptor.reset();
		}
		if (pasvPort != -1)
		{
			requestHandler.releasePassivePort(pasvPort);
			pasvPort = -1;
		}

		fileBytesTotal.QuadPart = 0;
		fileBytesSent = 0;
		std::cout << "Session destroyed" << std::endl;
	}

	void TinyFTPSession::start()
	{
		asio::async_write(socket, asio::buffer(WELCOME_STRING, strlen(WELCOME_STRING)),
			std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(),
				std::placeholders::_1));
		std::cout << "Session started" << std::endl;
	}

	void TinyFTPSession::handleReadControl(const asio::error_code& e, std::size_t bytes_transferred)
	{
		if (!e)
		{
			char * beginBuffer = buffer.data(), *endBuffer = buffer.data() + bytes_transferred;
			std::cout << "Control channel:" << std::string(beginBuffer, endBuffer);
			TinyFTPRequestParser::ParserResult result = requestParser.parse(request, beginBuffer, endBuffer);

			if (result == TinyFTPRequestParser::SUCCESS)
			{
				requestHandler.handleRequest(request, reply, this);
				if (!reply.content.empty()) 
				{
					asio::async_write(socket, asio::buffer(reply.content.data(), reply.content.size()), std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(), std::placeholders::_1));
				}
				else if (!dataOpInProgress)
				{
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
					std::cout << "Control channel: resuming" << std::endl;
				}
			}
			else if (result == TinyFTPRequestParser::FAIL)
			{
				std::cout << "Control channel: failed to parse" << std::endl;
				reply.content = StatusStrings::bad_request;
				asio::async_write(socket, asio::buffer(reply.content.data(), reply.content.size()), std::bind(&TinyFTPSession::handleWriteControl, shared_from_this(), std::placeholders::_1));
			}
			else if (result == TinyFTPRequestParser::NEEDMORE) 
			{
				std::cout << "Control channel: underrun" << std::endl;
				socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
				std::cout << "Control channel: resuming" << std::endl;
			}
		}
		else
		{
			std::cout << "Control channel: error on read" << std::endl;
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
			std::cout << "Data channel: upload: read " << bytes_transferred << " bytes" << std::endl;

			uploadBuffers.fullBuffers.emplace_back(bytes_transferred,uploadBuffers.curNetworkBuffer);
			uploadBuffers.curNetworkBuffer = 0;

			if (uploadBuffers.emptyBuffers.empty()) 
			{
				std::cout << "Data channel: upload: not enough network buffers, starved" << std::endl;
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
					std::cout << "Data channel: upload: socket ready" << std::endl;

					unsigned long bytes_available;
					ioctlsocket(socketData->native_handle(), FIONREAD, &bytes_available);
					if (bytes_available) 
					{
						std::cout << "Data channel: upload: socket ready: has data" << std::endl;
						auto buffersFront = std::move(uploadBuffers.emptyBuffers.front());
						uploadBuffers.curNetworkBuffer = buffersFront.second;
						uploadBuffers.emptyBuffers.pop_front();
						socketData->async_read_some(asio::buffer(uploadBuffers.curNetworkBuffer->data(), uploadBuffers.curNetworkBuffer->max_size()), std::bind(&TinyFTPSession::handleReadData, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
						std::cout << "Data channel: upload: reading" << std::endl;
					}
					else 
					{
						std::cout << "Data channel: upload: network read complete" << std::endl;
						uploadBuffers.noMoreReads = true;
					}
				}
				else 
				{
					std::cout << "Data channel: upload: timeout error, shutting down both sockets, closing files" << std::endl;
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
				std::cout << "Data channel: upload: no disk write in progress, starting" << std::endl;
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
			std::cout << "Data channel: upload: error, shutting down data socket" << std::endl;
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
			std::cout << "Disk write: written " << bytes_transferred << " bytes" <<std::endl;
			std::lock_guard<std::mutex> buffersGuard(uploadBuffers.queueMutex);

			uploadBuffers.emptyBuffers.emplace_back(0,uploadBuffers.curDiskBuffer);
			uploadBuffers.curDiskBuffer = 0;

			if (uploadBuffers.starved)
			{
				std::cout << "Disk write: network was starved for buffers" << std::endl;
				uploadBuffers.starved = false;
				auto buffersFront = std::move(uploadBuffers.emptyBuffers.front());
				uploadBuffers.curNetworkBuffer = buffersFront.second;
				uploadBuffers.emptyBuffers.pop_front();
				socketData->async_read_some(asio::buffer(uploadBuffers.curNetworkBuffer->data(), uploadBuffers.curNetworkBuffer->max_size()), std::bind(&TinyFTPSession::handleReadData, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
				std::cout << "Data channel: upload: restarting read from socket, after disk write released a free buffer" << std::endl;
			}

			if (uploadBuffers.fullBuffers.empty())
			{
				std::cout << "Disk write: write buffers empty: stopping disk write" << std::endl;
				uploadBuffers.writeInProgress = false;
				if (uploadBuffers.noMoreReads) 
				{
					std::cout << "Disk write: write buffers empty: network wont read more data" << std::endl;
					uploadBuffers.noMoreReads = false;
					uploadBuffers.starved = false;
					dataOpInProgress = false;
					closeDataSocket();
					if (fileToSend.is_open())
						fileToSend.close();
					std::cout << "Disk write: write buffers empty: network wont read more data: closing socket and file" << std::endl;

					asio::write(getSocket(), asio::buffer(StatusStrings::transfer_complete, sizeof(StatusStrings::transfer_complete) - 1), asio::transfer_all());
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
					std::cout << "Control channel: resuming" << std::endl;
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
				std::cout << "Disk write: writing data" << std::endl;
			}
		}
		else
		{
			std::cout << "Disk write: error: closing both sockets and file" << std::endl;
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
			std::cout << "Control channel: write complete" << std::endl;
			if (!dataOpInProgress)
			{
				socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()),
					std::bind(&TinyFTPSession::handleReadControl, shared_from_this(),
						std::placeholders::_1,
						std::placeholders::_2));
				std::cout << "Control channel: resuming" << std::endl;
			}
		}
		else 
		{
			std::cout << "Control channel: write error, closing both sockets and file" << std::endl;
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
			std::cout << "Data channel: write complete" << std::endl;
			bool expectedState = true;
			if (dataOpInProgress.compare_exchange_strong(expectedState, false) == true)
			{
				uint64_t fileBytesTotalBytes = fileBytesTotal.HighPart;
				fileBytesTotalBytes = fileBytesTotalBytes << 32;
				fileBytesTotalBytes += fileBytesTotal.LowPart;
				if (fileBytesTotalBytes - fileBytesSent <= TRANSMIT_FILE_LIMIT)
				{
					std::cout << "Data channel: write complete: file sent" << std::endl;
					fileBytesTotal.QuadPart = 0;
					fileBytesSent = 0;
					closeDataSocket();
					if (fileToSend.is_open())
						fileToSend.close();

					asio::write(getSocket(), asio::buffer(StatusStrings::transfer_complete, sizeof(StatusStrings::transfer_complete) - 1), asio::transfer_all());
					socket.async_read_some(asio::buffer(buffer.data(), buffer.max_size()), std::bind(&TinyFTPSession::handleReadControl, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
					std::cout << "Control channel: resuming" << std::endl;
				}
				else 
				{
					std::cout << "Data channel: write complete: sending next chunk" << std::endl;
					dataOpInProgress = true; // race! race here!
					fileBytesSent += TRANSMIT_FILE_LIMIT;
					transmit_file(*socketData, fileToSend, std::bind(&TinyFTPSession::handleWriteData, shared_from_this(), std::placeholders::_1), fileBytesSent, fileBytesTotal);
				}
			}
			else 
				abort();
		}
		else
		{
			std::cout << "Data channel: write error: closing both sockets and file" << std::endl;
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
		std::cout << "Pasv port opened " << port << std::endl;
		pasvPort = port;
		tcpAcceptor.reset(new asio::ip::tcp::acceptor(service, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), pasvPort)));
	}

	// starts data socket up in remote mode
	void TinyFTPSession::startDataSocketRemote()
	{
		std::cout << "Data channel: starting in standard mode" << std::endl;
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
		std::cout << "Data channel: started" << std::endl;
	}

	// starts data socket up in pasv mode
	void TinyFTPSession::startDataSocketPasv() 
	{
		std::cout << "Data channel: starting in pasv mode" << std::endl;
		socketData.reset(new asio::ip::tcp::socket(service));
		tcpAcceptor->accept(*socketData);
		socketData->set_option(asio::ip::tcp::no_delay(false));
		dataSocketConnected = true;
		std::cout << "Data channel: started" << std::endl;
	}

	// close data socket
	void TinyFTPSession::closeDataSocket() 
	{
		std::cout << "Data channel: closing socket" << std::endl;
		socketData->close();
		socketData.reset();
		if (tcpAcceptor.get()) 
		{
			tcpAcceptor->cancel();
			tcpAcceptor->close();
			tcpAcceptor.reset();
		}
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
		std::cout << "Data channel: starting file transfer" << std::endl;
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
		std::cout << "Data channel: starting file upload" << std::endl;
		if (fileToSend.is_open())
			fileToSend.close();
		fileBytesTotal.QuadPart = 0;
		fileBytesSent = 0;

		if (filename_.find("\\.\\") != std::string::npos)
			filename_.replace(filename_.find("\\.\\"), strlen("\\.\\"), "\\");
		asio::error_code ec;
		fileToSend.assign(::CreateFileA(filename_.c_str(), GENERIC_WRITE, 0, 0,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0), ec);
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
		std::cout << "Upload buffers created" << std::endl;
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
		std::cout << "Upload buffers destroyed" << std::endl;
	}

	void TinyFTPUploadBuffers::init() 
	{
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		emptyBuffers.emplace_front(0, new std::array<char, RECV_BUFFER_SIZE>());
		isInitialized = true;
		std::cout << "Upload buffers initialized" << std::endl;
	}

	bool TinyFTPSession::setCurDir(char * szNewCurDir) 
	{
		bool res = false;

		// replace all slashes
		size_t inputLen = strlen(szNewCurDir);
		for (size_t pos = 0; pos < inputLen; ++pos)
			if (szNewCurDir[pos] == '/')
				szNewCurDir[pos] = '\\';

		// check for first slash (absolute path), combine with root as necessary
		std::string newCombinedRoot;
		if (szNewCurDir[0] == '\\')
			newCombinedRoot = docRoot + szNewCurDir;
		else 
			newCombinedRoot = docRoot + curDirectory + (curDirectory =="\\" ? "" : "\\") + szNewCurDir;
		newCombinedRoot = std::regex_replace(newCombinedRoot, std::regex("\\\\[^\\\\]*\\\\\\.\\."), "");
		newCombinedRoot = std::regex_replace(newCombinedRoot, std::regex("\\\\\\\\"), "\\");
		while (newCombinedRoot.size() > 1 && *newCombinedRoot.rbegin() == '.')
			newCombinedRoot = newCombinedRoot.substr(0, newCombinedRoot.size() - 1);
		while (newCombinedRoot.size() > 1 && *newCombinedRoot.rbegin() == '\\')
			newCombinedRoot = newCombinedRoot.substr(0, newCombinedRoot.size() - 1);

		strncpy_s(szNewCurDir, TinyFTPSession::MAX_PATH_32K, newCombinedRoot.c_str(), newCombinedRoot.size());

		// try CreateFile for directory, if success - CloseFile and return true otherwise false
		HANDLE toClose = CreateFileA(szNewCurDir, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
		if (INVALID_HANDLE_VALUE != toClose)
		{
			CloseHandle(toClose);
			if (GetFileAttributesA(szNewCurDir) & FILE_ATTRIBUTE_DIRECTORY)
			{
				res = true;
				curDirectory = newCombinedRoot.substr(docRoot.size());
				while (curDirectory.size() > 1 && *curDirectory.rbegin() == '\\')
					curDirectory = curDirectory.substr(0, curDirectory.size() - 1);
			}
		}

		return res;
	}

	const char * TinyFTPSession::getCurDir() 
	{
		return curDirectory.c_str();
	}

	char * TinyFTPSession::translatePath(char * pathToTranslate) 
	{
		// replace all slashes
		size_t inputLen = strlen(pathToTranslate);
		if (inputLen) 
		{
			for (size_t pos = 0; pos < inputLen; ++pos)
				if (pathToTranslate[pos] == '/')
				{
					pathToTranslate[pos] = '\\';
				}
		}

		// check for first slash (absolute path), combine with root as necessary
		std::string newCombinedPath;
		if (pathToTranslate[0] == '\\')
			newCombinedPath = docRoot + pathToTranslate;
		else
			newCombinedPath = docRoot + curDirectory + "\\" + pathToTranslate;

		while (*newCombinedPath.rbegin() == '\\' && newCombinedPath.size() > 1)
			newCombinedPath = newCombinedPath.substr(0,newCombinedPath.size() - 1);
		newCombinedPath = std::regex_replace(newCombinedPath, std::regex("\\\\[^\\\\]*\\\\\\.\\."), "");
		newCombinedPath = std::regex_replace(newCombinedPath, std::regex("\\\\\\\\"), "\\");
		newCombinedPath = std::regex_replace(newCombinedPath, std::regex("\\.\\\\"), "");

		strncpy_s(pathToTranslate, TinyFTPSession::MAX_PATH_32K, "\\\\?\\", strlen("\\\\?\\"));
		strncpy_s(pathToTranslate + strlen("\\\\?\\"), TinyFTPSession::MAX_PATH_32K - strlen("\\\\?\\"), newCombinedPath.c_str(), newCombinedPath.size());

		return pathToTranslate;
	}

}