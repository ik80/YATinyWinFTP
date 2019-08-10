#ifndef IK80_TINYFTPSERVER_H_
#define IK80_TINYFTPSERVER_H_

#include <memory>
#include <thread>

#include <asio\io_service.hpp>
#include <asio\ip\tcp.hpp>

#include "TinyFTPSession.h"
#include "TinyFTPRequestHandler.h"

namespace TinyWinFTP 
{
	class TinyFTPServer
	{
	public:
		TinyFTPServer(std::string in_docRoot, short port);

		// start the server
		void run();

		// stop the server
		void stop();

	private:
		/// Get an io_service to use.
		asio::io_service& getIoService();

		// async accept incoming clients
		void doAccept();

		/// The pool of io_services.
		std::vector<std::shared_ptr<asio::io_service> > ioServices;

		/// The work that keeps the io_services running.
		std::vector<std::shared_ptr<asio::io_service::work> > works;

		/// The next io_service to use for a connection.
		std::size_t nextIoService;

		/// The parser for the incoming request.
		TinyFTPRequestParser requestParser;

		// RequestHandler for the server
		TinyFTPRequestHandler requestHandler;

		// acceptor and listener socket
		std::shared_ptr<asio::ip::tcp::acceptor> tcpAcceptor;
		std::shared_ptr<asio::ip::tcp::socket> listenSocket;

		std::string docRoot;
	};
}

#endif // IK80_TINYFTPSERVER_H_
