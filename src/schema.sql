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
	id integer GENERATED ALWAYS AS IDENTITY,
	value text NOT NULL,
	type entity NOT NULL,
	detail jsonb
)
PARTITION BY LIST (type);

CREATE TABLE hosts PARTITION OF entities
FOR VALUES IN ('host');

ALTER TABLE hosts
	ADD CONSTRAINT pk_hosts PRIMARY KEY (id);

CREATE TABLE virtual_hosts PARTITION OF entities
FOR VALUES IN ('virtual_host');

ALTER TABLE virtual_hosts
	ADD CONSTRAINT pk_virtual_hosts PRIMARY KEY (id);

CREATE TABLE paths PARTITION OF entities
FOR VALUES IN ('path');

ALTER TABLE paths
	ADD CONSTRAINT pk_paths PRIMARY KEY (id);

CREATE TABLE query_strings PARTITION OF entities
FOR VALUES IN ('query_string');

ALTER TABLE query_strings
	ADD CONSTRAINT pk_query_strings PRIMARY KEY (id);

CREATE TABLE referrers PARTITION OF entities
FOR VALUES IN ('referrer');

ALTER TABLE referrers
	ADD CONSTRAINT pk_referrers PRIMARY KEY (id);

CREATE TABLE user_agents PARTITION OF entities
FOR VALUES IN ('user_agent');

ALTER TABLE user_agents
	ADD CONSTRAINT pk_user_agents PRIMARY KEY (id);

CREATE TABLE content_types PARTITION OF entities
FOR VALUES IN ('content_type');

ALTER TABLE content_types
	ADD CONSTRAINT pk_content_types PRIMARY KEY (id);

CREATE TABLE bad_lines PARTITION OF entities
FOR VALUES IN ('unparsable_line', 'uninsertable_line');

ALTER TABLE bad_lines
	ADD CONSTRAINT pk_bad_lines PRIMARY KEY (id);

CREATE UNIQUE INDEX uni_entities_value ON entities(MD5(value), type);

CREATE INDEX idx_entities_retryinsert ON bad_lines(id)
WHERE
	type = 'uninsertable_line' AND detail IS NULL;

CREATE OR REPLACE FUNCTION entity(newValue text, newType entity)
	RETURNS TABLE(
		id integer,
		nulldetail boolean
	)
	AS $$
DECLARE
	now timestamp without time zone;
	recid integer;
	nulldetail boolean;
BEGIN
	INSERT INTO entities(value, type)
	SELECT
		newValue,
		newType
	WHERE
		NOT EXISTS (
			SELECT
			FROM
				entities
			WHERE
				md5(value) = md5(newValue)
				AND type = newType)
	ON CONFLICT
		DO NOTHING
	RETURNING
		entities.id,
		entities.detail IS NULL
	INTO
		recid,
		nulldetail;
	IF recid IS NULL THEN
		RETURN QUERY
		SELECT
			e.id,
			e.detail IS NULL
		FROM
			entities e
		WHERE
			md5(e.value) = md5(newValue)
			AND e.type = newType;
	ELSE
		RETURN QUERY
	VALUES (recid,
		nulldetail);
	END IF;
END;
$$
LANGUAGE plpgSQL
RETURNS NULL ON NULL INPUT;

CREATE TABLE access_log(
	hostname integer NOT NULL,
	virtual_host integer NOT NULL,
	remoteip inet NOT NULL,
	request_time timestamp(6) NOT NULL,
	method http_verb NOT NULL,
	protocol protocol NOT NULL,
	path integer NOT NULL,
	query_string integer,
	status smallint NOT NULL,
	size bigint NOT NULL,
	duration interval second(6) NOT NULL,
	referrer integer,
	user_agent integer,
	content_type integer,
	CONSTRAINT fk_access_log_hostname FOREIGN KEY (hostname) REFERENCES hosts(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_virtualhost FOREIGN KEY (virtual_host) REFERENCES virtual_hosts(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_path FOREIGN KEY (path) REFERENCES paths(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_query_string FOREIGN KEY (query_string) REFERENCES query_strings(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_referrer FOREIGN KEY (referrer) REFERENCES referrers(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_user_agent FOREIGN KEY (user_agent) REFERENCES user_agents(id) ON UPDATE CASCADE,
	CONSTRAINT fk_access_log_content_type FOREIGN KEY (content_type) REFERENCES content_types(id) ON UPDATE CASCADE
);

CREATE INDEX idx_access_log_path ON access_log(path);

CREATE INDEX idx_access_log_query_string ON access_log(query_string);

CREATE INDEX idx_access_log_request_time ON access_log USING BRIN(request_time) WITH (autosummarize = TRUE);

CREATE INDEX idx_access_log_virtual_host ON access_log(virtual_host);

CREATE OR REPLACE VIEW access_log_view AS
SELECT
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
	LEFT OUTER JOIN hosts h ON l.hostname = h.id
	LEFT OUTER JOIN virtual_hosts v ON l.virtual_host = v.id
	LEFT OUTER JOIN paths p ON l.path = p.id
	LEFT OUTER JOIN query_strings q ON l.query_string = q.id
	LEFT OUTER JOIN referrers r ON l.referrer = r.id
	LEFT OUTER JOIN user_agents u ON l.user_agent = u.id
	LEFT OUTER JOIN content_types c ON l.content_type = c.id;
