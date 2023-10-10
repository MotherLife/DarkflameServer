#pragma once

#include <string>
#include <vector>

#include "RakNetTypes.h"

#include "Structures.h"

enum eConnectionTypes {
	NONE,
	MYSQL,
	SQLITE
};

class DatabaseBase {
public:
	virtual void Connect() = 0;
	virtual void Destroy() = 0;

	// Server Get
	virtual SocketDescriptor GetMasterServerIP() = 0;

	// Server Set
	virtual void CreateServer(const std::string& name, const std::string& ip, uint16_t port, uint32_t state, uint32_t version) = 0;
	virtual void SetServerIpAndPortByName(const std::string& name, const std::string& ip, uint16_t port) = 0;
	
	// Misc
	virtual void InsertIntoActivityLog(uint32_t playerId, uint32_t activityId, uint32_t timestamp, uint32_t zoneId) = 0;
	virtual void InsertIntoCommandLog(uint32_t playerId, const std::string& command) = 0;

	// Character Get
	virtual CharacterInfo GetCharacterInfoByID(uint32_t id) = 0;
	virtual CharacterInfo GetCharacterInfoByName(const std::string& name) = 0;
	virtual std::string GetCharacterXMLByID(uint32_t id) = 0;
	virtual std::vector<std::string> GetAllCharacterNames() = 0;
	virtual std::vector<CharacterInfo> GetAllCharactersByAccountID(uint32_t accountId) = 0;
	virtual bool IsCharacterNameAvailable(const std::string& name) = 0;

	// Charater Write
	virtual void CreateCharacterXML(uint32_t id, const std::string& xml) = 0;
	virtual void UpdateCharacterXML(uint32_t id, const std::string& xml) = 0;
	virtual void CreateCharacter(uint32_t id, uint32_t account_id, const std::string& name, const std::string& pending_name, bool needs_rename, uint64_t last_login) = 0;

	// Character Delete
	virtual void DeleteCharacter(uint32_t id) = 0;

	// Friends Get
	virtual bool AreBestFriends(uint32_t charId1, uint32_t charId2) = 0;

	// Account Get
	virtual AccountInfo GetAccountByName(const std::string& name) = 0;
	virtual AccountInfo GetAccountByID(uint32_t id) = 0;
	virtual uint32_t GetLatestCharacterOfAccount(uint32_t id) = 0;

	// Pet Write
	virtual void CreatePetName(uint64_t id, const std::string& name, bool approved) = 0;

	// Pet Delete
	virtual void DeletePetName(uint64_t id) = 0;

	// Pet Get
	virtual PetName GetPetName(uint64_t id) = 0;

	// Keys Get
	virtual bool IsKeyActive(uint32_t id) = 0;

	// Object ID tracker Get
	virtual uint32_t GetObjectIDTracker() = 0;

	// Object ID tracker Set
	virtual void SetObjectIDTracker(uint32_t id) = 0;
private:

};
