INSERT INTO
access_log(hostname, virtual_host, remoteip, request_time, method, path, query_string, protocol, status, size, duration,
		referrer, user_agent)
VALUES(?, ?, ?, TO_TIMESTAMP(? / 1000000.0) at time zone 'utc', ?, ?, ?, ?, ?, ?, ? * '1us'::interval, ?, ?)
