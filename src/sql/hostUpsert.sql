INSERT INTO entities(type, value, detail)
	VALUES ('host', $1, jsonb_build_object('sysname', $2::text, 'release', $3::text,
	'version', $4::text, 'machine', $5::text, 'domainname', $6::text))
ON CONFLICT (md5(value))
	DO UPDATE SET
		detail = jsonb_build_object('sysname', $2::text, 'release', $3::text, 'version',
		$4::text, 'machine', $5::text, 'domainname', $6::text)
	RETURNING
		id
