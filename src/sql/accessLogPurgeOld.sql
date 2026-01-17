WITH scope AS (
	SELECT
		id
	FROM
		access_log
	ORDER BY
		id
	LIMIT ?
),
scoperange AS (
	SELECT
		min(id) minid,
		max(id) maxid
	FROM
		scope)
DELETE FROM access_log USING scoperange
WHERE request_time < CURRENT_DATE - ?::interval
	AND access_log.id BETWEEN scoperange.minid AND scoperange.maxid
