SELECT
	id,
	value,
	cast(detail ->> 'hostnameId' AS int) AS hostnameId
FROM
	entities
WHERE
	type = 'uninsertable_line'
	AND detail ->> 'retriedAt' IS NULL
ORDER BY
	id
LIMIT ?
FOR UPDATE
