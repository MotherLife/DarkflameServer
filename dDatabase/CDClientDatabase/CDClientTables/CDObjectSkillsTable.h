#pragma once

// Custom Classes
#include "CDTable.h"

struct CDObjectSkills {
	uint32_t objectTemplate;        //!< The LOT of the item
	uint32_t skillID;               //!< The Skill ID of the object
	uint32_t castOnType;            //!< ???
	uint32_t AICombatWeight;        //!< ???
};

class CDObjectSkillsTable : public CDTable<CDObjectSkillsTable> {
private:
	std::vector<CDObjectSkills> entries;

public:
	void LoadValuesFromDatabase();
	// Queries the table with a custom "where" clause
	std::vector<CDObjectSkills> Query(std::function<bool(CDObjectSkills)> predicate);

	// Gets all the entries in the table
	const std::vector<CDObjectSkills>& GetEntries() const;

};

