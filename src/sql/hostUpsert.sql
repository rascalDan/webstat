INSERT INTO entities(id, type, value, detail)
	VALUES ($1, 'host', $2, jsonb_build_object('sysname', $3::text, 'release', $4::text,
	'version', $5::text, 'machine', $6::text, 'domainname', $7::text))
ON CONFLICT ON CONSTRAINT pk_entities
	DO UPDATE SET
		detail = jsonb_build_object('sysname', $3::text, 'release', $4::text, 'version',
		$5::text, 'machine', $6::text, 'domainname', $7::text)
