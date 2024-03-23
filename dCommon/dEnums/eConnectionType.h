#ifndef __ECONNECTIONTYPE__H__
#define __ECONNECTIONTYPE__H__

enum class eConnectionType : uint16_t {
	SERVER = 0,
	AUTH,
	CHAT,
	UNUSED,
	WORLD,
	CLIENT,
	MASTER
};

#endif  //!__ECONNECTIONTYPE__H__
