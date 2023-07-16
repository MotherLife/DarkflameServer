#pragma once

// Custom Classes
#include "CDTable.h"

struct CDActivityRewards {
	unsigned int objectTemplate;        //!< The object template (?)
	unsigned int ActivityRewardIndex;   //!< The activity reward index
	int activityRating;                 //!< The activity rating
	unsigned int LootMatrixIndex;       //!< The loot matrix index
	unsigned int CurrencyIndex;         //!< The currency index
	unsigned int ChallengeRating;       //!< The challenge rating
	std::string description;            //!< The description
};

namespace CDActivityRewardsTable {
	void LoadTableIntoMemory();
	// Queries the table with a custom "where" clause
	std::vector<CDActivityRewards> Query(std::function<bool(CDActivityRewards)> predicate);
};
