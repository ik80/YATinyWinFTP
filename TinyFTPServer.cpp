#include "TinyFTPServer.h"

namespace TinyWinFTP
{

	TinyFTPServer::TinyFTPServer(std::string in_docRoot, short port) : nextIoService(0), docRoot(in_docRoot)
	{
		size_t pool_size = std::thread::hardware_concurrency();
		for (std::size_t i = 0; i < pool_size; ++i)
		{
			std::shared_ptr<asio::io_service> newService = std::make_shared<asio::io_service>();
			std::shared_ptr<asio::io_service::work> newWork = std::make_shared<asio::io_service::work>(*newService);
			ioServices.push_back(newService);
			works.push_back(newWork);
		}
		tcpAcceptor.reset(new asio::ip::tcp::acceptor(*ioServices[0], asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)));
		listenSocket.reset(new asio::ip::tcp::socket(*ioServices[0]));
	}

	/// Get an io_service to use.
	asio::io_service& TinyFTPServer::getIoService()
	{
		nextIoService = (nextIoService + 1) % ioServices.size();
		return *(ioServices[nextIoService]);
	}

	void TinyFTPServer::doAccept()
	{
		tcpAcceptor->async_accept(*listenSocket,
			[this](std::error_code ec)
		{
			if (!ec)
			{
				std::make_shared<TinyFTPSession>(getIoService(), std::move(*listenSocket), &requestHandler, requestParser, docRoot)->start();
			}
			doAccept();
		});
	}


	void TinyFTPServer::run()
	{
		// Create a pool of threads to run all of the io_services.
		std::vector<std::shared_ptr<std::thread> > threads;
		for (std::size_t i = 0; i < ioServices.size(); ++i)
		{
			asio::io_service * pService = &(*ioServices[i]);
			std::shared_ptr<std::thread> newThread(new std::thread([pService] {pService->run();}));
			threads.push_back(newThread);
		}

		// start accepting stuff
		doAccept();

		// Wait for all threads in the pool to exit.
		for (std::size_t i = 0; i < threads.size(); ++i)
			threads[i]->join();
	}

	void TinyFTPServer::stop()
	{
		// Explicitly stop all io_services.
		for (std::size_t i = 0; i < ioServices.size(); ++i)
			ioServices[i]->stop();
	}

}
