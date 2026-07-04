UPDATE
	entities
SET
	detail = detail || jsonb_build_object('retriedAt', CURRENT_TIMESTAMP at time zone 'utc', 'error', ?::text)
WHERE
	id = ?
