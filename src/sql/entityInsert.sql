INSERT INTO entities(id, type, value)
	VALUES (?, ?, ?)
ON CONFLICT
	DO NOTHING
