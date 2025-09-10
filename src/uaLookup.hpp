#pragma once

#include "util.hpp"
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace WebStat {
	class CurlError : public std::runtime_error {
	public:
		explicit CurlError(CURLcode code, const char * msg) : std::runtime_error {msg}, code(code) { }

		CURLcode code;
	};

	using CurlPtr = std::unique_ptr<CURL, DeleteWith<&curl_easy_cleanup>>;
	using CurlMimePtr = std::unique_ptr<curl_mime, DeleteWith<&curl_mime_free>>;
	using CurlErrorBuf = std::array<char, CURL_ERROR_SIZE>;
	using CurlMultiPtr = std::unique_ptr<CURLM, DeleteWith<&curl_multi_cleanup>>;

	class CurlOperation {
	public:
		CurlOperation();
		void addForm(const char * name, std::string_view data);

		CurlPtr hnd;
		CurlMimePtr mime;
		CurlErrorBuf err;
		std::string result;
	};

	std::unique_ptr<CurlOperation> curlGetUserAgentDetail(std::string_view uas, const char * baseUrl);
}
