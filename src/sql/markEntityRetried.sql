UPDATE
	entities
SET
	detail = jsonb_build_object('retriedAt', CURRENT_TIMESTAMP at time zone 'utc', 'error', ?::text)
WHERE
	id = ?
