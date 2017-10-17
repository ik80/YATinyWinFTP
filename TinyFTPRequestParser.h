#ifndef IK80_TINYFTPREQUESTPARSER_H_
#define IK80_TINYFTPREQUESTPARSER_H_

#include "TinyFTPRequest.h"
#include "FastSubstringMatcher.h"

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
		FastSubstringMatcher matcher;
	};

}
#endif // HTTP_SERVER2_REQUEST_PARSER_HPP
