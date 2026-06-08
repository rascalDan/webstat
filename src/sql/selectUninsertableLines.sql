SELECT
	id,
	value
FROM
	entities
WHERE
	type = 'uninsertable_line'
	AND detail IS NULL
ORDER BY
	id
LIMIT ?
FOR UPDATE
