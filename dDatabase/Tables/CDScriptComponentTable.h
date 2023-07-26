#pragma once

// Custom Classes
#include "CDTable.h"

struct CDScriptComponent {
	unsigned int id;                        //!< The component ID
	std::string script_name;           		//!< The script name
	std::string client_script_name;   		//!< The client script name
};

class CDScriptComponentTable : public CDTable<CDScriptComponentTable> {
private:
	std::map<unsigned int, CDScriptComponent> entries;
public:
	void LoadValuesFromDatabase();
	// Gets an entry by scriptID
	const std::optional<CDScriptComponent> GetByID(unsigned int id);
};

