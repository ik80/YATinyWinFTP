#include <iostream>

#include "TinyFTPServer.h"

TinyWinFTP::TinyFTPServer * gpServer;

void consoleHandler() 
{
	gpServer->stop();
}


int main(int argc, char * argv[])
{
	if (argc != 3) 
	{
		std::cout << "Usage " << argv[0] << " <Directory> <Port>" << std::endl;
		return -1;
	}

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleHandler, TRUE);
	TinyWinFTP::TinyFTPServer server(argv[1], atoi(argv[2]));
	gpServer = &server; // nasty all around
	server.run();
    return 0;
}

