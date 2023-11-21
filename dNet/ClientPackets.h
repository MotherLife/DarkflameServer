/*
 * Darkflame Universe
 * Copyright 2018
 */

#ifndef CLIENTPACKETS_H
#define CLIENTPACKETS_H

#include "RakNetTypes.h"
#include "eGuildCreationResponse.h"
#include "dCommonVars.h"

namespace ClientPackets {
	void HandleChatMessage(const SystemAddress& sysAddr, Packet* packet);
	void HandleClientPositionUpdate(const SystemAddress& sysAddr, Packet* packet);
	void HandleChatModerationRequest(const SystemAddress& sysAddr, Packet* packet);
	void SendTop5HelpIssues(Packet* packet);

	// Guild stuff
	void HandleGuildCreation(Packet* packet);
	void SendGuildCreateResponse(const SystemAddress& sysAddr, eGuildCreationResponse guildResponse, LWOOBJID guild_id, std::u16string& guildName);
};

#endif // CLIENTPACKETS_H
