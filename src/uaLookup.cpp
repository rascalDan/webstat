#include "uaLookup.hpp"
#include "util.hpp"
#include <memory>
#include <string_view>

namespace WebStat {
	namespace {
		size_t
		stringAppend(const char * ptr, size_t size, size_t nmemb, std::string * result)
		{
			result->append(ptr, nmemb * size);
			return nmemb * size;
		}

		void
		addForm(curl_mime * mime, const char * name, const std::string_view data)
		{
			auto part = curl_mime_addpart(mime);
			curl_mime_data(part, data.data(), data.length());
			curl_mime_name(part, name);
		};
	}

	std::string
	getUserAgentDetail(const std::string_view uas, const char * baseUrl)
	{
		std::string result;
		std::array<char, CURL_ERROR_SIZE> err {};
		std::unique_ptr<CURL, DeleteWith<&curl_easy_cleanup>> hnd {curl_easy_init()};
		curl_easy_setopt(hnd.get(), CURLOPT_URL, baseUrl);
		curl_easy_setopt(hnd.get(), CURLOPT_NOPROGRESS, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_USERAGENT, "WebStat/0; +https://git.randomdan.homeip.net/repo/webstat/");
		curl_easy_setopt(hnd.get(), CURLOPT_MAXREDIRS, 50L);
		curl_easy_setopt(hnd.get(), CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_ERRORBUFFER, err.data());
		curl_easy_setopt(hnd.get(), CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(hnd.get(), CURLOPT_WRITEFUNCTION, &stringAppend);
		std::unique_ptr<curl_mime, DeleteWith<&curl_mime_free>> mime {curl_mime_init(hnd.get())};
		addForm(mime.get(), "uas", uas);
		addForm(mime.get(), "getJSON", "all");
		curl_easy_setopt(hnd.get(), CURLOPT_MIMEPOST, mime.get());

		if (CURLcode ret = curl_easy_perform(hnd.get()); ret != CURLE_OK) {
			throw CurlError {ret, err.data()};
		}

		return result;
	}
}
