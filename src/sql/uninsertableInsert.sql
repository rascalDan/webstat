INSERT INTO entities(type, value, detail)
	VALUES ('uninsertable_line', $1, jsonb_build_object('hostnameId', $2::int, 'error', $3::text,
	'timestamp', CURRENT_TIMESTAMP))
RETURNING
	id
