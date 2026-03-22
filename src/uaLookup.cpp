#include "uaLookup.hpp"
#include "sql.hpp"
#include <connection.h>
#include <dbTypes.h>
#include <modifycommand.h>

namespace WebStat {
	UserAgentLookupOperation::UserAgentLookupOperation(Crc32Value userAgentEntityId) : entityId {userAgentEntityId} { }

	void
	UserAgentLookupOperation::whenComplete(DB::Connection * dbconn) const
	{
		auto upd = dbconn->modify(SQL::ENTITY_UPDATE_DETAIL, SQL::ENTITY_UPDATE_DETAIL_OPTS);
		bindMany(upd, 0, entityId, result);
		upd->execute();
	}

	std::unique_ptr<CurlOperation>
	curlGetUserAgentDetail(Crc32Value entityId, const std::string_view uas, const char * baseUrl)
	{
		auto request = std::make_unique<UserAgentLookupOperation>(entityId);

		curl_easy_setopt(request->hnd.get(), CURLOPT_URL, baseUrl);
		curl_easy_setopt(
				request->hnd.get(), CURLOPT_USERAGENT, "WebStat/0; +https://git.randomdan.homeip.net/repo/webstat/");
		request->addForm("uas", uas);
		request->addForm("getJSON", "all");

		return request;
	}
}
