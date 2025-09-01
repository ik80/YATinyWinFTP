#ifndef IK80_TINYFTPSERVER_H_
#define IK80_TINYFTPSERVER_H_

#include <memory>
#include <thread>

#include <asio\io_context.hpp>
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
		/// Get an io_context to use.
		asio::io_context& getIoService();

		// async accept incoming clients
		void doAccept();

		/// The pool of io_contexts.
		std::vector<std::shared_ptr<asio::io_context> > ioServices;

		/// The work that keeps the io_contexts running.

		typedef asio::executor_work_guard<
			asio::io_context::executor_type> io_context_work;

		std::vector<io_context_work > works;

		/// The next io_context to use for a connection.
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
