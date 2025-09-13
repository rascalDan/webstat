#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include <connection_fwd.h>
#include <curl/curl.h>
#include <memory>

namespace WebStat {
	class UserAgentLookupOperation : public CurlOperation {
	public:
		UserAgentLookupOperation(Crc32Value entityId);

		void whenComplete(DB::Connection *) const override;

		Crc32Value entityId;
	};

	std::unique_ptr<CurlOperation> curlGetUserAgentDetail(
			Crc32Value entityId, std::string_view uas, const char * baseUrl);
}
