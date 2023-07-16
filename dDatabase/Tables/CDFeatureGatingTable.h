#pragma once

// Custom Classes
#include "CDTable.h"

struct CDFeatureGating {
	std::string featureName;
	int32_t major;
	int32_t current;
	int32_t minor;
	std::string description;
};

namespace CDFeatureGatingTable {
private:
	std::vector<CDFeatureGating> entries;

public:
	void LoadTableIntoMemory();
	// Queries the table with a custom "where" clause
	std::vector<CDFeatureGating> Query(std::function<bool(CDFeatureGating)> predicate);

	bool FeatureUnlocked(const std::string& feature) const;

	std::vector<CDFeatureGating> GetEntries(void) const;
};
