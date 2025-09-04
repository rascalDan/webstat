#include "uaLookup.hpp"
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
	}

	CurlOperation::CurlOperation() : hnd {curl_easy_init()}, err {}
	{
		curl_easy_setopt(hnd.get(), CURLOPT_NOPROGRESS, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_MAXREDIRS, 50L);
		curl_easy_setopt(hnd.get(), CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(hnd.get(), CURLOPT_ERRORBUFFER, err.data());
		curl_easy_setopt(hnd.get(), CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(hnd.get(), CURLOPT_WRITEFUNCTION, &stringAppend);
	}

	void
	CurlOperation::addForm(const char * name, const std::string_view data)
	{
		if (!mime) {
			mime.reset(curl_mime_init(hnd.get()));
			curl_easy_setopt(hnd.get(), CURLOPT_MIMEPOST, mime.get());
		}
		auto part = curl_mime_addpart(mime.get());
		curl_mime_data(part, data.data(), data.length());
		curl_mime_name(part, name);
	}

	std::unique_ptr<CurlOperation>
	curlGetUserAgentDetail(const std::string_view uas, const char * baseUrl)
	{
		auto request = std::make_unique<CurlOperation>();

		curl_easy_setopt(request->hnd.get(), CURLOPT_URL, baseUrl);
		curl_easy_setopt(
				request->hnd.get(), CURLOPT_USERAGENT, "WebStat/0; +https://git.randomdan.homeip.net/repo/webstat/");
		request->addForm("uas", uas);
		request->addForm("getJSON", "all");

		return request;
	}
}
