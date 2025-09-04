#pragma once

#include <curl/curl.h>
#include <stdexcept>
#include <string>

namespace WebStat {
	class CurlError : public std::runtime_error {
	public:
		explicit CurlError(CURLcode code, const char * msg) : std::runtime_error {msg}, code(code) { }

		CURLcode code;
	};

	std::string getUserAgentDetail(std::string_view uas, const char * baseUrl = "https://useragentstring.com");
}
