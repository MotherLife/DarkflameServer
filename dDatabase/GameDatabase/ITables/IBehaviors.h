#ifndef __IBEHAVIORS__H__
#define __IBEHAVIORS__H__

#include <cstdint>

#include "dCommonVars.h"

class IBehaviors {
public:
	struct Info {
		int32_t behaviorId{};
		uint32_t characterId{};
		std::string behaviorInfo;
	};

	// This Add also takes care of updating if it exists.
	virtual void AddBehavior(const Info& info) = 0;
	virtual std::string GetBehavior(const int32_t behaviorId) = 0;
	virtual void RemoveBehavior(const int32_t behaviorId) = 0;
};

#endif  //!__IBEHAVIORS__H__
