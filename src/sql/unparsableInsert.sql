INSERT INTO entities(type, value, detail)
	VALUES ('unparsable_line', $1, jsonb_build_object('hostnameId', $2::int, 'timestamp', CURRENT_TIMESTAMP))
RETURNING
	id
