#pragma once

#ifndef __EITEMTYPE__H__
#define __EITEMTYPE__H__

#include <cstdint>

enum class eItemType : int32_t {
	UNKNOWN = -1,
	BRICK = 1,
	HAT,
	HAIR,
	NECK,
	LEFT_HAND,
	RIGHT_HAND,
	LEGS,
	LEFT_TRINKET,
	RIGHT_TRINKET,
	BEHAVIOR,
	PROPERTY,
	MODEL,
	COLLECTIBLE,
	CONSUMABLE,
	CHEST,
	EGG,
	PET_FOOD,
	QUEST_OBJECT,
	PET_INVENTORY_ITEM,
	PACKAGE,
	LOOT_MODEL,
	VEHICLE,
	LUP_MODEL,
	MOUNT
};

#endif  //!__EITEMTYPE__H__
