#pragma once

#include "util.hpp"
#include <connection_fwd.h>
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace WebStat {
	class CurlError : public std::runtime_error {
	public:
		explicit CurlError(CURLcode errorCode, const char * msg) : std::runtime_error {msg}, code(errorCode) { }

		CURLcode code;
	};

	using CurlPtr = std::unique_ptr<CURL, DeleteWith<&curl_easy_cleanup>>;
	using CurlMimePtr = std::unique_ptr<curl_mime, DeleteWith<&curl_mime_free>>;
	using CurlErrorBuf = std::array<char, CURL_ERROR_SIZE>;
	using CurlMultiPtr = std::unique_ptr<CURLM, DeleteWith<&curl_multi_cleanup>>;

	class CurlOperation {
	public:
		CurlOperation();
		virtual ~CurlOperation() = default;

		SPECIAL_MEMBERS_DEFAULT_MOVE_NO_COPY(CurlOperation);

		void addForm(const char * name, std::string_view data);

		virtual void whenComplete(DB::Connection *) const = 0;
		virtual void onError(DB::Connection *) const;

		CurlPtr hnd;
		CurlMimePtr mime;
		CurlErrorBuf err;
		std::string result;
	};
}
