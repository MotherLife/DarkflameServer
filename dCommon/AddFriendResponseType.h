#pragma once

#ifndef __ADDFRIENDRESPONSETYPE__H__
#define __ADDFRIENDRESPONSETYPE__H__

#include <cstdint>

enum class AddFriendResponseType : uint8_t {
	ACCEPTED = 0,
	ALREADYFRIEND,
	INVALIDCHARACTER,
	GENERALERROR,
	YOURFRIENDSLISTFULL,
	THEIRFRIENDLISTFULL,
	DECLINED,
	BUSY,
	NOTONLINE,
	WAITINGAPPROVAL,
	MYTHRAN,
	CANCELLED,
	FRIENDISFREETRIAL
};

#endif  //!__ADDFRIENDRESPONSETYPE__H__
