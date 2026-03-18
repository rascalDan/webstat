CREATE TYPE http_verb AS ENUM(
	'GET',
	'HEAD',
	'OPTIONS',
	'TRACE',
	'PUT',
	'DELETE',
	'POST',
	'PATCH',
	'CONNECT',
	'PROPFIND'
);

CREATE TYPE protocol AS ENUM(
	'HTTP/1.0',
	'HTTP/1.1',
	'HTTP/1.2',
	'HTTP/1.3',
	'HTTP/2.0',
	'HTTPS/3.0'
);

CREATE TYPE entity AS ENUM(
	'host',
	'virtual_host',
	'path',
	'query_string',
	'referrer',
	'user_agent',
	'unparsable_line',
	'uninsertable_line',
	'content_type'
);

CREATE TABLE entities(
	id oid NOT NULL,
	value text NOT NULL,
	type entity NOT NULL,
	detail jsonb,
	CONSTRAINT pk_entities PRIMARY KEY (id),
	CONSTRAINT uni_entities_value UNIQUE (value)
);

CREATE TABLE access_log(
	id bigint GENERATED ALWAYS AS IDENTITY,
	hostname oid NOT NULL,
	virtual_host oid NOT NULL,
	remoteip inet NOT NULL,
	request_time timestamp(6) NOT NULL,
	method http_verb NOT NULL,
	protocol protocol NOT NULL,
	path oid NOT NULL,
	query_string oid,
	status smallint NOT NULL,
	size int NOT NULL,
	duration interval second(6) NOT NULL,
	referrer oid,
	user_agent oid,
	content_type oid,
	CONSTRAINT pk_access_log PRIMARY KEY (id),
	CONSTRAINT fk_access_log_hostname FOREIGN KEY (hostname) REFERENCES entities(id),
	CONSTRAINT fk_access_log_virtualhost FOREIGN KEY (virtual_host) REFERENCES entities(id),
	CONSTRAINT fk_access_log_path FOREIGN KEY (path) REFERENCES entities(id),
	CONSTRAINT fk_access_log_query_string FOREIGN KEY (query_string) REFERENCES entities(id),
	CONSTRAINT fk_access_log_referrer FOREIGN KEY (referrer) REFERENCES entities(id),
	CONSTRAINT fk_access_log_user_agent FOREIGN KEY (user_agent) REFERENCES entities(id),
	CONSTRAINT fk_access_log_content_type FOREIGN KEY (content_type) REFERENCES entities(id)
);

CREATE OR REPLACE VIEW access_log_view AS
SELECT
	l.id,
	h.id hostname_id,
	h.value hostname,
	v.id virtual_host_id,
	v.value virtual_host,
	remoteip::text,
	request_time,
	method,
	protocol,
	p.id path_id,
	p.value path,
	q.id query_string_id,
	q.value query_string,
	status,
	size,
	duration,
	r.id referrer_id,
	r.value referrer,
	u.id user_agent_id,
	u.value user_agent,
	c.id content_type_id,
	c.value content_type
FROM
	access_log l
	LEFT OUTER JOIN entities h ON l.hostname = h.id
	LEFT OUTER JOIN entities v ON l.virtual_host = v.id
	LEFT OUTER JOIN entities p ON l.path = p.id
	LEFT OUTER JOIN entities q ON l.query_string = q.id
	LEFT OUTER JOIN entities r ON l.referrer = r.id
	LEFT OUTER JOIN entities u ON l.user_agent = u.id
	LEFT OUTER JOIN entities c ON l.content_type = c.id;
