#ifndef IK80_TINYFTPREPLY_H_
#define IK80_TINYFTPREPLY_H_

#include <string>
#include <vector>
#include <asio.hpp>

namespace TinyWinFTP
{
	/// A reply to be sent to a client.
	struct TinyFTPReply
	{
		/// The content to be sent in the reply.
		std::string content;
	};

	namespace StatusStrings
	{
		const char ok[] = "200 OK\r\n";
		const char error[] = "550 Error\r\n";
		const char opening_binary_connection[] = "150 Opening BINARY mode data connection\r\n";
		const char opening_connection[] = "150 Opening connection\r\n";
		const char error_not_a_plain_file[] = "550 not a plain file.\r\n";
		const char transfer_complete[] = "226 Transfer Complete\r\n";
		const char login_accepted[] = "331 pretend login accepted\r\n";
		const char user_logged_in[] = "230 fake user logged in\r\n";
		const char syst_string[] = "215 WIN32 ftpdmin REMASTERED ed.\r\n";
		const char path_perm_error[] = "550 Path permission error\r\n";
		const char delete_successful[] = "250 DELE command successful.\r\n";
		const char dir_created[] = "257 Directory created\r\n";
		const char dir_removed[] = "250 RMD command successful\r\n";
		const char file_exists[] = "350 File Exists\r\n";
		const char rnto_successful[] = "250 RNTO command successful\r\n";
		const char aborted[] = "226 Aborted\r\n";
		const char cwd_failed[] = "550 Could not change directory\r\n";
		const char cwd_successful[] = "250 CWD command successful\r\n";
		const char type_successful[] = "200 Type set to I\r\n";
		const char port_successful[] = "200 PORT command successful\r\n";
		const char unknown_command[] = "500 command not recognized\r\n";
		const char unimplemented_command[] = "500 command not implemented\r\n";
		const char bad_request[] = "550 bad request\r\n";
		const char bye[] = "221 goodbye\r\n";
	} // namespace stock_replies
}
#endif // IK80_TINYFTPREPLY_H_
