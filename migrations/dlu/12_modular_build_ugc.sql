CREATE TABLE IF NOT EXISTS ugc_modular_build (
	ugc_id BIGINT NOT NULL PRIMARY KEY,
	character_id BIGINT NOT NULL REFERENCES charinfo(id) ON DELETE CASCADE,
	ldf_config VARCHAR(60) NOT NULL
);
