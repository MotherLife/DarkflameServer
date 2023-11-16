CREATE TABLE IF NOT EXISTS guilds (
	id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
	name VARCHAR(35) NOT NULL,
	owner_id BIGINT NOT NULL REFERENCES charinfo(id) ON DELETE CASCADE,
	motd TEXT,
	reputation INT NOT NULL DEFAULT 0,
	created BIGINT UNSIGNED NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS guild_members (
	guild_id BIGINT NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
	character_id BIGINT NOT NULL REFERENCES charinfo(id) ON DELETE CASCADE,
	rank INT NOT NULL DEFAULT 4,
	joined BIGINT UNSIGNED NOT NULL DEFAULT 0
	PRIMARY KEY (guild_id, character_id)
);