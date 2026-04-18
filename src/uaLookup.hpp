#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include <connection_fwd.h>
#include <curl/curl.h>
#include <memory>

namespace WebStat {
	class UserAgentLookupOperation : public CurlOperation {
	public:
		UserAgentLookupOperation(EntityId entityId);

		void whenComplete(DB::Connection *) const override;

		EntityId entityId;
	};

	std::unique_ptr<CurlOperation> curlGetUserAgentDetail(
			EntityId entityId, std::string_view uas, const char * baseUrl);
}
