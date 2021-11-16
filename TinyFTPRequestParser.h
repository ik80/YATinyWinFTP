#ifndef IK80_TINYFTPREQUESTPARSER_H_
#define IK80_TINYFTPREQUESTPARSER_H_

#include <unordered_map>

#include "TinyFTPRequest.h"

namespace TinyWinFTP
{
	/// Parser for incoming requests.
	class TinyFTPRequestParser
	{
	public:
		enum ParserResult 
		{
			SUCCESS = 0,
			FAIL,
			NEEDMORE,
			MAX
		};

		/// Construct ready to parse the request method.
		TinyFTPRequestParser();

		/// Reset to initial parser state.
		void reset();

		ParserResult parse(TinyFTPRequest& req, char*& begin, char*& end);
	private:
		std::unordered_map<std::string, size_t> commands;
	};

}
#endif // HTTP_SERVER2_REQUEST_PARSER_HPP
