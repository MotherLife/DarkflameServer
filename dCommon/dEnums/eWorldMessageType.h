#ifndef __EWORLDMESSAGETYPE__H__
#define __EWORLDMESSAGETYPE__H__

#include <cstdint>

#include "magic_enum.hpp"

enum class eWorldMessageType : uint32_t {
	VALIDATION = 1,  // 				Session info
	CHARACTER_LIST_REQUEST,
	CHARACTER_CREATE_REQUEST,
	LOGIN_REQUEST,  // 			Character selected
	GAME_MSG,
	CHARACTER_DELETE_REQUEST,
	CHARACTER_RENAME_REQUEST,
	HAPPY_FLOWER_MODE_NOTIFY,
	SLASH_RELOAD_MAP,  // 			Reload map cmp
	SLASH_PUSH_MAP_REQUEST,  // 	Push map req cmd
	SLASH_PUSH_MAP,  // 			Push map cmd
	SLASH_PULL_MAP,  // 			Pull map cmd
	LOCK_MAP_REQUEST,
	GENERAL_CHAT_MESSAGE,  // 		General chat message
	HTTP_MONITOR_INFO_REQUEST,
	SLASH_DEBUG_SCRIPTS,  // 		Debug scripts cmd
	MODELS_CLEAR,
	EXHIBIT_INSERT_MODEL,
	LEVEL_LOAD_COMPLETE,  // 		Character data request
	TMP_GUILD_CREATE,
	ROUTE_PACKET,  // 				Social?
	POSITION_UPDATE,
	MAIL,
	WORD_CHECK, // 				Whitelist word check
	STRING_CHECK,  // 				Whitelist string check
	GET_PLAYERS_IN_ZONE,
	REQUEST_UGC_MANIFEST_INFO,
	BLUEPRINT_GET_ALL_DATA_REQUEST,
	CANCEL_MAP_QUEUE,
	HANDLE_FUNNESS,
	FAKE_PRG_CSR_MESSAGE,
	REQUEST_FREE_TRIAL_REFRESH,
	GM_SET_FREE_TRIAL_STATUS,
	UI_HELP_TOP_5 = 91
};

template <>
struct magic_enum::customize::enum_range<eWorldMessageType> {
	static constexpr int min = 0;
	static constexpr int max = 91;
};

#endif  //!__EWORLDMESSAGETYPE__H__
