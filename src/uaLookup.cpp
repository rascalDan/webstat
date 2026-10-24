#include "uaLookup.hpp"
#include "sql.hpp"
#include <connection.h>
#include <dbTypes.h> // IWYU pragma: keep
#include <modifycommand.h> // IWYU pragma: keep

namespace WebStat {
	UserAgentLookupOperation::UserAgentLookupOperation(EntityId userAgentEntityId) : entityId {userAgentEntityId} { }

	void
	UserAgentLookupOperation::whenComplete(DB::Connection * dbconn) const
	{
		const auto upd = dbconn->modify(SQL::ENTITY_UPDATE_DETAIL, SQL::ENTITY_UPDATE_DETAIL_OPTS);
		bindMany(upd, 0, entityId, result);
		upd->execute();
	}

	std::unique_ptr<CurlOperation>
	curlGetUserAgentDetail(EntityId entityId, const std::string_view uas, const char * baseUrl)
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
