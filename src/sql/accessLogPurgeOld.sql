WITH delete_batch AS (
	SELECT
		ctid
	FROM
		access_log
	WHERE
		request_time < CURRENT_DATE - ?::interval
	ORDER BY
		request_time
	FOR UPDATE
LIMIT ?)
DELETE FROM access_log AS al USING delete_batch AS del
WHERE al.ctid = del.ctid
