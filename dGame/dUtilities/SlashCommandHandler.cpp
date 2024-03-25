/*
 * Darkflame Universe
 * Copyright 2018
 */

#include "SlashCommandHandler.h"

#include <sstream>
#include <iostream>
#include <fstream>
#include <exception>
#include "dZoneManager.h"

#include <stdio.h>  /* defines FILENAME_MAX */
#ifdef _WIN32
#include <direct.h>
#define GetCurrentDir _getcwd
#else
#include <unistd.h>
#define GetCurrentDir getcwd
#endif

#include "Metrics.hpp"

#include "User.h"
#include "UserManager.h"
#include "BitStream.h"
#include "dCommonVars.h"
#include "GeneralUtils.h"
#include "Entity.h"
#include "EntityManager.h"
#include "Logger.h"
#include "WorldPackets.h"
#include "GameMessages.h"
#include "CDClientDatabase.h"
#include "ZoneInstanceManager.h"
#include "ControllablePhysicsComponent.h"
#include "NiPoint3.h"
#include "NiQuaternion.h"
#include "ChatPackets.h"
#include "InventoryComponent.h"
#include "Game.h"
#include "CharacterComponent.h"
#include "Database.h"
#include "DestroyableComponent.h"
#include "dServer.h"
#include "MissionComponent.h"
#include "Mail.h"
#include "dpWorld.h"
#include "Item.h"
#include "PropertyManagementComponent.h"
#include "BitStreamUtils.h"
#include "Loot.h"
#include "EntityInfo.h"
#include "LUTriggers.h"
#include "PhantomPhysicsComponent.h"
#include "ProximityMonitorComponent.h"
#include "dpShapeSphere.h"
#include "PossessableComponent.h"
#include "PossessorComponent.h"
#include "HavokVehiclePhysicsComponent.h"
#include "BuffComponent.h"
#include "SkillComponent.h"
#include "VanityUtilities.h"
#include "ScriptedActivityComponent.h"
#include "LevelProgressionComponent.h"
#include "AssetManager.h"
#include "BinaryPathFinder.h"
#include "dConfig.h"
#include "eBubbleType.h"
#include "Amf3.h"
#include "MovingPlatformComponent.h"
#include "eMissionState.h"
#include "TriggerComponent.h"
#include "eServerDisconnectIdentifiers.h"
#include "eObjectBits.h"
#include "eGameMasterLevel.h"
#include "eReplicaComponentType.h"
#include "RenderComponent.h"
#include "eControlScheme.h"
#include "eConnectionType.h"
#include "eChatInternalMessageType.h"
#include "eMasterMessageType.h"
#include "PlayerManager.h"

#include "CDRewardCodesTable.h"
#include "CDObjectsTable.h"
#include "CDZoneTableTable.h"
#include "ePlayerFlag.h"
#include "dNavMesh.h"

namespace {
	std::map<std::string, Command> RegisteredCommands;
}

void SlashCommandHandler::RegisterCommand(Command info, std::string command) {
	LOG_DEBUG("Registering SlashCommand: %s", command.c_str());
	RegisteredCommands.emplace(std::make_pair(command, info));
};

// DEV Commands
void DEVGMCommands::SetGMLevel(Entity* entity, const std::string args) {
	User* user = UserManager::Instance()->GetUser(entity->GetSystemAddress());

	const auto level_intermed = GeneralUtils::TryParse<uint32_t>(args);
	if (!level_intermed) {
		GameMessages::SendSlashCommandFeedbackText(entity, u"Invalid GM level.");
		return;
	}
	eGameMasterLevel level = static_cast<eGameMasterLevel>(level_intermed.value());

#ifndef DEVELOPER_SERVER
	if (user->GetMaxGMLevel() == eGameMasterLevel::JUNIOR_DEVELOPER) {
		level = eGameMasterLevel::CIVILIAN;
	}
#endif

	if (level > user->GetMaxGMLevel()) level = user->GetMaxGMLevel();

	if (level == entity->GetGMLevel()) return;
	bool success = user->GetMaxGMLevel() >= level;

	if (success) {
		WorldPackets::SendGMLevelChange(entity->GetSystemAddress(), success, user->GetMaxGMLevel(), entity->GetGMLevel(), level);
		GameMessages::SendChatModeUpdate(entity->GetObjectID(), level);
		entity->SetGMLevel(level);
		LOG("User %s (%i) has changed their GM level to %i for charID %llu", user->GetUsername().c_str(), user->GetAccountID(), level, entity->GetObjectID());
	}

#ifndef DEVELOPER_SERVER
	if ((entity->GetGMLevel() > user->GetMaxGMLevel()) || (entity->GetGMLevel() > eGameMasterLevel::CIVILIAN && user->GetMaxGMLevel() == eGameMasterLevel::JUNIOR_DEVELOPER)) {
		WorldPackets::SendGMLevelChange(entity->GetSystemAddress(), true, user->GetMaxGMLevel(), entity->GetGMLevel(), eGameMasterLevel::CIVILIAN);
		GameMessages::SendChatModeUpdate(entity->GetObjectID(), eGameMasterLevel::CIVILIAN);
		entity->SetGMLevel(eGameMasterLevel::CIVILIAN);

		GameMessages::SendToggleGMInvis(entity->GetObjectID(), false, UNASSIGNED_SYSTEM_ADDRESS);

		GameMessages::SendSlashCommandFeedbackText(entity, u"Your game master level has been changed, you may not be able to use all commands.");
	}
#endif
}

void DEVGMCommands::ToggleNameplate(Entity* entity, const std::string args) {
	if ((Game::config->GetValue("allow_nameplate_off") != "1" && entity->GetGMLevel() < eGameMasterLevel::DEVELOPER)) return;

	auto* character = entity->GetCharacter();
	if (character && character->GetBillboardVisible()) {
		character->SetBillboardVisible(false);
		GameMessages::SendSlashCommandFeedbackText(entity, u"Your nameplate has been turned off and is not visible to players currently in this zone.");
	} else {
		character->SetBillboardVisible(true);
		GameMessages::SendSlashCommandFeedbackText(entity, u"Your nameplate is now on and visible to all players.");
	}
}

void DEVGMCommands::ToggleSkipCinematics(Entity* entity, const std::string args) {
		if (Game::config->GetValue("allow_players_to_skip_cinematics") != "1" && entity->GetGMLevel() < eGameMasterLevel::DEVELOPER) return;
		auto* character = entity->GetCharacter();
		if (!character) return;
		bool current = character->GetPlayerFlag(ePlayerFlag::DLU_SKIP_CINEMATICS);
		character->SetPlayerFlag(ePlayerFlag::DLU_SKIP_CINEMATICS, !current);
		if (!current) {
			GameMessages::SendSlashCommandFeedbackText(entity, u"You have elected to skip cinematics. Note that not all cinematics can be skipped, but most will be skipped now.");
		} else {
			GameMessages::SendSlashCommandFeedbackText(entity, u"Cinematics will no longer be skipped.");
		}
}

// Greater Than Zero Commands

// GM Zero Commands
void GMZeroCommands::Help(Entity* entity, const std::string args) {
	if (args.empty()) {
		std::ostringstream helpMessage;
		helpMessage << "Commands:\n*";
		for (auto& [commandCall, command] : RegisteredCommands) {
			// TODO: Limit dispplaying commands based on GM level they require
			// if (RegisteredCommands[args].requiredLevel > entity->GetGMLevel()) continue;
			helpMessage << "\t" << commandCall << ": " << command.description << "\n*";
		}
		GameMessages::SendSlashCommandFeedbackText(entity, GeneralUtils::ASCIIToUTF16(helpMessage.str().substr(0, helpMessage.str().size() - 3)));
	} else {
		if (RegisteredCommands.contains(args)) {
			if (entity->GetGMLevel() >= RegisteredCommands[args].requiredLevel) {
				RegisteredCommands[args].help;
				GameMessages::SendSlashCommandFeedbackText(entity, GeneralUtils::ASCIIToUTF16(RegisteredCommands[args].help));
			}
		} else {
			// We don't need to tell normies commands don't exist
			if (entity->GetGMLevel() == eGameMasterLevel::CIVILIAN) return;
			std::ostringstream feedback;
			feedback << "Command " << std::quoted(args) << " does not exist!";
			GameMessages::SendSlashCommandFeedbackText(entity, GeneralUtils::ASCIIToUTF16(feedback.str()));
		}
	}
}

void SlashCommandHandler::Startup() {

	// Register Dev Commands
	Command SetGMLevelCommand;
	SetGMLevelCommand.description = "Set the players GM Level";
	SetGMLevelCommand.help = "Within the authorized range of levels for the current account, changes the character's game master level to the specified value. This is required to use certain commands.";
	SetGMLevelCommand.requiredLevel = eGameMasterLevel::CIVILIAN;
	SetGMLevelCommand.handle = DEVGMCommands::SetGMLevel;
	RegisterCommand(SetGMLevelCommand, "setgmlevel");
	RegisterCommand(SetGMLevelCommand, "makegm");
	RegisterCommand(SetGMLevelCommand, "gmlevel");
	
	Command ToggleNameplateCommand;
	ToggleNameplateCommand.description = "Toggles the visibility of a players nameplate";
	ToggleNameplateCommand.help = "Turns the nameplate above your head that is visible to other players off and on.";
	ToggleNameplateCommand.requiredLevel = eGameMasterLevel::CIVILIAN;
	ToggleNameplateCommand.handle = DEVGMCommands::ToggleNameplate;
	RegisterCommand(ToggleNameplateCommand, "togglenameplate");
	RegisterCommand(ToggleNameplateCommand, "tnp");

	Command ToggleSkipCinematicsCommand;
	ToggleSkipCinematicsCommand.description = "Toggles skipping cinematics";
	ToggleSkipCinematicsCommand.help = "Skips mission and world load related cinematics.";
	ToggleSkipCinematicsCommand.requiredLevel = eGameMasterLevel::CIVILIAN;
	ToggleSkipCinematicsCommand.handle = DEVGMCommands::ToggleSkipCinematics;
	RegisterCommand(ToggleSkipCinematicsCommand, "toggleskipcinematics");
	RegisterCommand(ToggleSkipCinematicsCommand, "tsc");

	// Register Greater Than Zero Commands

	// Register GM Zero Commands
	Command HelpCommand;
	HelpCommand.description = "Display command help";
	HelpCommand.help = "Displays a list of commands and their descriptions if no command are given. If an command is given, displays detailed info about that command.";
	HelpCommand.requiredLevel = eGameMasterLevel::CIVILIAN;
	HelpCommand.handle = GMZeroCommands::Help;
	RegisterCommand(HelpCommand, "help");
	RegisterCommand(HelpCommand, "h");
}

void SlashCommandHandler::HandleChatCommand(const std::u16string& chat, Entity* entity, const SystemAddress& sysAddr) {
	auto input = GeneralUtils::UTF16ToWTF8(chat);
	if (input.empty() || input.front() != u'/') return;
	std::string command = input.substr(1, input.find(' ') - 1);

	std::string args = input.substr(input.find(' ') + 1, std::string::npos);
	if (args.front() == '/') args.clear();
	LOG("Handling command \"%s\" with args \"%s\"", command.c_str(), args.c_str());

	if (RegisteredCommands.contains(command)) {
		if (entity->GetGMLevel() >= RegisteredCommands[command].requiredLevel) {
			RegisteredCommands[command].handle(entity, args);
		} else {
			// We don't need to tell normies they aren't high enough level
			if (entity->GetGMLevel() == eGameMasterLevel::CIVILIAN) return;
			std::ostringstream feedback;
			feedback << "You are not high enough GM level to use " << std::quoted(command) << "";
			GameMessages::SendSlashCommandFeedbackText(entity, GeneralUtils::ASCIIToUTF16(feedback.str()));
		}

	} else {
		// We don't need to tell normies commands don't exist
		if (entity->GetGMLevel() == eGameMasterLevel::CIVILIAN) return;
		std::ostringstream feedback;
		feedback << "Command " << std::quoted(command) << " does not exist!";
		GameMessages::SendSlashCommandFeedbackText(entity, GeneralUtils::ASCIIToUTF16(feedback.str()));
	}

	return;


	// 	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// 	//HANDLE ALL NON GM SLASH COMMANDS RIGHT HERE!
	// 	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	// 	if (chatCommand == "pvp") {
	// 		auto* character = entity->GetComponent<CharacterComponent>();

	// 		if (character == nullptr) {
	// 			LOG("Failed to find character component!");
	// 			return;
	// 		}

	// 		character->SetPvpEnabled(!character->GetPvpEnabled());
	// 		Game::entityManager->SerializeEntity(entity);

	// 		std::stringstream message;
	// 		message << character->GetName() << " changed their PVP flag to " << std::to_string(character->GetPvpEnabled()) << "!";

	// 		ChatPackets::SendSystemMessage(UNASSIGNED_SYSTEM_ADDRESS, GeneralUtils::UTF8ToUTF16(message.str()), true);

	// 		return;
	// 	}

	// 	if (chatCommand == "who") {
	// 		ChatPackets::SendSystemMessage(
	// 			sysAddr,
	// 			u"Players in this instance: (" + GeneralUtils::to_u16string(PlayerManager::GetAllPlayers().size()) + u")"
	// 		);

	// 		for (auto* player : PlayerManager::GetAllPlayers()) {
	// 			const auto& name = player->GetCharacter()->GetName();

	// 			ChatPackets::SendSystemMessage(
	// 				sysAddr,
	// 				GeneralUtils::UTF8ToUTF16(player == entity ? name + " (you)" : name)
	// 			);
	// 		}
	// 	}

	// 	if (chatCommand == "ping") {
	// 		if (!args.empty() && args[0] == "-l") {
	// 			std::stringstream message;
	// 			message << "Your latest ping: " << std::to_string(Game::server->GetLatestPing(sysAddr)) << "ms";

	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(message.str()));
	// 		} else {
	// 			std::stringstream message;
	// 			message << "Your average ping: " << std::to_string(Game::server->GetPing(sysAddr)) << "ms";

	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(message.str()));
	// 		}
	// 		return;
	// 	}

	// 	if (chatCommand == "fix-stats") {
	// 		// Reset skill component and buff component
	// 		auto* skillComponent = entity->GetComponent<SkillComponent>();
	// 		auto* buffComponent = entity->GetComponent<BuffComponent>();
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();

	// 		// If any of the components are nullptr, return
	// 		if (skillComponent == nullptr || buffComponent == nullptr || destroyableComponent == nullptr) {
	// 			return;
	// 		}

	// 		// Reset skill component
	// 		skillComponent->Reset();

	// 		// Reset buff component
	// 		buffComponent->Reset();

	// 		// Fix the destroyable component
	// 		destroyableComponent->FixStats();
	// 	}

	// 	if (chatCommand == "credits" || chatCommand == "info") {
	// 		const auto& customText = chatCommand == "credits" ? VanityUtilities::ParseMarkdown((BinaryPathFinder::GetBinaryDir() / "vanity/CREDITS.md").string()) : VanityUtilities::ParseMarkdown((BinaryPathFinder::GetBinaryDir() / "vanity/INFO.md").string());

	// 		{
	// 			AMFArrayValue args;

	// 			args.Insert("state", "Story");

	// 			GameMessages::SendUIMessageServerToSingleClient(entity, entity->GetSystemAddress(), "pushGameState", args);
	// 		}

	// 		entity->AddCallbackTimer(0.5f, [customText, entity]() {
	// 			AMFArrayValue args;

	// 			args.Insert("visible", true);
	// 			args.Insert("text", customText);

	// 			LOG("Sending %s", customText.c_str());

	// 			GameMessages::SendUIMessageServerToSingleClient(entity, entity->GetSystemAddress(), "ToggleStoryBox", args);
	// 			});

	// 		return;
	// 	}

	// 	if (chatCommand == "leave-zone" || chatCommand == "leavezone") {
	// 		const auto currentZone = Game::zoneManager->GetZone()->GetZoneID().GetMapID();
	// 		LWOMAPID newZone = 0;

	// 		if (currentZone == 1001 || currentZone % 100 == 0) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"You are not in an instanced zone.");
	// 			return;
	// 		} else {
	// 			newZone = (currentZone / 100) * 100;
	// 		}
	// 		// If new zone would be inaccessible, then default to Avant Gardens.
	// 		if (!Game::zoneManager->CheckIfAccessibleZone(newZone)) newZone = 1100;

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Leaving zone...");

	// 		const auto objid = entity->GetObjectID();

	// 		ZoneInstanceManager::Instance()->RequestZoneTransfer(Game::server, newZone, 0, false, [objid](bool mythranShift, uint32_t zoneID, uint32_t zoneInstance, uint32_t zoneClone, std::string serverIP, uint16_t serverPort) {
	// 			auto* entity = Game::entityManager->GetEntity(objid);

	// 			if (entity == nullptr) {
	// 				return;
	// 			}

	// 			const auto sysAddr = entity->GetSystemAddress();

	// 			LOG("Transferring %s to Zone %i (Instance %i | Clone %i | Mythran Shift: %s) with IP %s and Port %i", entity->GetCharacter()->GetName().c_str(), zoneID, zoneInstance, zoneClone, mythranShift == true ? "true" : "false", serverIP.c_str(), serverPort);

	// 			if (entity->GetCharacter()) {
	// 				entity->GetCharacter()->SetZoneID(zoneID);
	// 				entity->GetCharacter()->SetZoneInstance(zoneInstance);
	// 				entity->GetCharacter()->SetZoneClone(zoneClone);
	// 			}

	// 			entity->GetCharacter()->SaveXMLToDatabase();

	// 			WorldPackets::SendTransferToWorld(sysAddr, serverIP, serverPort, mythranShift);
	// 			});
	// 	}

	// 	if (chatCommand == "join" && !args.empty()) {
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Requesting private map...");
	// 		const auto& password = args[0];

	// 		ZoneInstanceManager::Instance()->RequestPrivateZone(Game::server, false, password, [=](bool mythranShift, uint32_t zoneID, uint32_t zoneInstance, uint32_t zoneClone, std::string serverIP, uint16_t serverPort) {
	// 			LOG("Transferring %s to Zone %i (Instance %i | Clone %i | Mythran Shift: %s) with IP %s and Port %i", sysAddr.ToString(), zoneID, zoneInstance, zoneClone, mythranShift == true ? "true" : "false", serverIP.c_str(), serverPort);

	// 			if (entity->GetCharacter()) {
	// 				entity->GetCharacter()->SetZoneID(zoneID);
	// 				entity->GetCharacter()->SetZoneInstance(zoneInstance);
	// 				entity->GetCharacter()->SetZoneClone(zoneClone);
	// 			}

	// 			entity->GetCharacter()->SaveXMLToDatabase();

	// 			WorldPackets::SendTransferToWorld(sysAddr, serverIP, serverPort, mythranShift);
	// 			});
	// 	}

	// 	if (user->GetMaxGMLevel() == eGameMasterLevel::CIVILIAN || entity->GetGMLevel() >= eGameMasterLevel::CIVILIAN) {
	// 		if (chatCommand == "die") {
	// 			entity->Smash(entity->GetObjectID());
	// 		}

	// 		if (chatCommand == "resurrect") {
	// 			ScriptedActivityComponent* scriptedActivityComponent = Game::zoneManager->GetZoneControlObject()->GetComponent<ScriptedActivityComponent>();

	// 			if (scriptedActivityComponent) { // check if user is in activity world and if so, they can't resurrect
	// 				ChatPackets::SendSystemMessage(sysAddr, u"You cannot resurrect in an activity world.");
	// 				return;
	// 			}

	// 			GameMessages::SendResurrect(entity);
	// 		}

	// 		if (chatCommand == "requestmailcount") {
	// 			Mail::HandleNotificationRequest(entity->GetSystemAddress(), entity->GetObjectID());
	// 		}

	// 		if (chatCommand == "instanceinfo") {
	// 			const auto zoneId = Game::zoneManager->GetZone()->GetZoneID();

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Map: " + (GeneralUtils::to_u16string(zoneId.GetMapID())) + u"\nClone: " + (GeneralUtils::to_u16string(zoneId.GetCloneID())) + u"\nInstance: " + (GeneralUtils::to_u16string(zoneId.GetInstanceID())));
	// 		}

	// 		if (entity->GetGMLevel() == eGameMasterLevel::CIVILIAN) return;
	// 	}

	// 	if (chatCommand == "resetmission" && args.size() >= 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto missionId = GeneralUtils::TryParse<uint32_t>(args[0]);
	// 		if (!missionId) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid mission ID.");
	// 			return;
	// 		}

	// 		auto* missionComponent = entity->GetComponent<MissionComponent>();
	// 		if (!missionComponent) return;
	// 		missionComponent->ResetMission(missionId.value());
	// 	}

	// 	// Log command to database
	// 	Database::Get()->InsertSlashCommandUsage(entity->GetObjectID(), chatCommand);

	// 	if (chatCommand == "setminifig" && args.size() == 2 && entity->GetGMLevel() >= eGameMasterLevel::FORUM_MODERATOR) { // could break characters so only allow if GM > 0
	// 		const auto minifigItemIdExists = GeneralUtils::TryParse<int32_t>(args[1]);
	// 		if (!minifigItemIdExists) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid Minifig Item Id ID.");
	// 			return;
	// 		}
	// 		const int32_t minifigItemId = minifigItemIdExists.value();
	// 		Game::entityManager->DestructEntity(entity, sysAddr);
	// 		auto* charComp = entity->GetComponent<CharacterComponent>();
	// 		std::string lowerName = args[0];
	// 		if (lowerName.empty()) return;
	// 		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
	// 		if (lowerName == "eyebrows") {
	// 			charComp->m_Character->SetEyebrows(minifigItemId);
	// 		} else if (lowerName == "eyes") {
	// 			charComp->m_Character->SetEyes(minifigItemId);
	// 		} else if (lowerName == "haircolor") {
	// 			charComp->m_Character->SetHairColor(minifigItemId);
	// 		} else if (lowerName == "hairstyle") {
	// 			charComp->m_Character->SetHairStyle(minifigItemId);
	// 		} else if (lowerName == "pants") {
	// 			charComp->m_Character->SetPantsColor(minifigItemId);
	// 		} else if (lowerName == "lefthand") {
	// 			charComp->m_Character->SetLeftHand(minifigItemId);
	// 		} else if (lowerName == "mouth") {
	// 			charComp->m_Character->SetMouth(minifigItemId);
	// 		} else if (lowerName == "righthand") {
	// 			charComp->m_Character->SetRightHand(minifigItemId);
	// 		} else if (lowerName == "shirtcolor") {
	// 			charComp->m_Character->SetShirtColor(minifigItemId);
	// 		} else if (lowerName == "hands") {
	// 			charComp->m_Character->SetLeftHand(minifigItemId);
	// 			charComp->m_Character->SetRightHand(minifigItemId);
	// 		} else {
	// 			Game::entityManager->ConstructEntity(entity);
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid Minifig item to change, try one of the following: Eyebrows, Eyes, HairColor, HairStyle, Pants, LeftHand, Mouth, RightHand, Shirt, Hands");
	// 			return;
	// 		}

	// 		Game::entityManager->ConstructEntity(entity);
	// 		ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(lowerName) + u" set to " + (GeneralUtils::to_u16string(minifigItemId)));

	// 		GameMessages::SendToggleGMInvis(entity->GetObjectID(), false, UNASSIGNED_SYSTEM_ADDRESS); // need to retoggle because it gets reenabled on creation of new character
	// 	}

	// 	if ((chatCommand == "playanimation" || chatCommand == "playanim") && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		std::u16string anim = GeneralUtils::ASCIIToUTF16(args[0], args[0].size());
	// 		RenderComponent::PlayAnimation(entity, anim);
	// 		auto* possessorComponent = entity->GetComponent<PossessorComponent>();
	// 		if (possessorComponent) {
	// 			auto* possessedComponent = Game::entityManager->GetEntity(possessorComponent->GetPossessable());
	// 			if (possessedComponent) RenderComponent::PlayAnimation(possessedComponent, anim);
	// 		}
	// 	}

	// 	if (chatCommand == "list-spawns" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		for (const auto& pair : Game::entityManager->GetSpawnPointEntities()) {
	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(pair.first));
	// 		}

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Current: " + GeneralUtils::ASCIIToUTF16(entity->GetCharacter()->GetTargetScene()));

	// 		return;
	// 	}

	// 	if (chatCommand == "unlock-emote" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto emoteID = GeneralUtils::TryParse<int32_t>(args.at(0));

	// 		if (!emoteID) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid emote ID.");
	// 			return;
	// 		}

	// 		entity->GetCharacter()->UnlockEmote(emoteID.value());
	// 	}

	// 	if (chatCommand == "force-save" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		entity->GetCharacter()->SaveXMLToDatabase();
	// 	}

	// 	if (chatCommand == "kill" && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Brutally murdering that player, if online on this server.");

	// 		auto* player = PlayerManager::GetPlayer(args[0]);
	// 		if (player) {
	// 			player->Smash(entity->GetObjectID());
	// 			ChatPackets::SendSystemMessage(sysAddr, u"It has been done, do you feel good about yourself now?");
	// 			return;
	// 		}

	// 		ChatPackets::SendSystemMessage(sysAddr, u"They were saved from your carnage.");
	// 		return;
	// 	}

	// 	if (chatCommand == "speedboost" && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto boostOptional = GeneralUtils::TryParse<float>(args[0]);
	// 		if (!boostOptional) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid boost.");
	// 			return;
	// 		}
	// 		const float boost = boostOptional.value();

	// 		auto* controllablePhysicsComponent = entity->GetComponent<ControllablePhysicsComponent>();

	// 		if (!controllablePhysicsComponent) return;
	// 		controllablePhysicsComponent->SetSpeedMultiplier(boost);

	// 		// speedboost possessables
	// 		auto possessor = entity->GetComponent<PossessorComponent>();
	// 		if (possessor) {
	// 			auto possessedID = possessor->GetPossessable();
	// 			if (possessedID != LWOOBJID_EMPTY) {
	// 				auto possessable = Game::entityManager->GetEntity(possessedID);
	// 				if (possessable) {
	// 					auto* possessControllablePhysicsComponent = possessable->GetComponent<ControllablePhysicsComponent>();
	// 					if (possessControllablePhysicsComponent) {
	// 						possessControllablePhysicsComponent->SetSpeedMultiplier(boost);
	// 					}
	// 				}
	// 			}
	// 		}

	// 		Game::entityManager->SerializeEntity(entity);
	// 	}

	// 	if (chatCommand == "freecam" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto state = !entity->GetVar<bool>(u"freecam");
	// 		entity->SetVar<bool>(u"freecam", state);

	// 		GameMessages::SendSetPlayerControlScheme(entity, static_cast<eControlScheme>(state ? 9 : 1));

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Toggled freecam.");
	// 		return;
	// 	}

	// 	if (chatCommand == "setcontrolscheme" && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto scheme = GeneralUtils::TryParse<uint32_t>(args[0]);

	// 		if (!scheme) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid control scheme.");
	// 			return;
	// 		}

	// 		GameMessages::SendSetPlayerControlScheme(entity, static_cast<eControlScheme>(scheme.value()));

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Switched control scheme.");
	// 		return;
	// 	}

	// 	if (chatCommand == "approveproperty" && entity->GetGMLevel() >= eGameMasterLevel::LEAD_MODERATOR) {

	// 		if (PropertyManagementComponent::Instance() != nullptr) {
	// 			PropertyManagementComponent::Instance()->UpdateApprovedStatus(true);
	// 		}

	// 		return;
	// 	}

	// 	if (chatCommand == "setuistate" && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		AMFArrayValue uiState;

	// 		uiState.Insert("state", args.at(0));

	// 		GameMessages::SendUIMessageServerToSingleClient(entity, sysAddr, "pushGameState", uiState);

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Switched UI state.");

	// 		return;
	// 	}

	// 	if (chatCommand == "toggle" && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		AMFArrayValue amfArgs;

	// 		amfArgs.Insert("visible", true);

	// 		GameMessages::SendUIMessageServerToSingleClient(entity, sysAddr, args[0], amfArgs);

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Toggled UI state.");

	// 		return;
	// 	}

	// 	if ((chatCommand == "setinventorysize" || chatCommand == "setinvsize") && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		const auto sizeOptional = GeneralUtils::TryParse<uint32_t>(args[0]);
	// 		if (!sizeOptional) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid size.");
	// 			return;
	// 		}
	// 		const uint32_t size = sizeOptional.value();

	// 		eInventoryType selectedInventory = eInventoryType::ITEMS;

	// 		// a possible inventory was provided if we got more than 1 argument
	// 		if (args.size() >= 2) {
	// 			selectedInventory = GeneralUtils::TryParse<eInventoryType>(args.at(1)).value_or(eInventoryType::INVALID);
	// 			if (selectedInventory == eInventoryType::INVALID) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid inventory.");
	// 				return;
	// 			} else {
	// 				// In this case, we treat the input as a string and try to find it in the reflection list
	// 				std::transform(args.at(1).begin(), args.at(1).end(), args.at(1).begin(), ::toupper);
	// 				for (uint32_t index = 0; index < NUMBER_OF_INVENTORIES; index++) {
	// 					if (std::string_view(args.at(1)) == std::string_view(InventoryType::InventoryTypeToString(static_cast<eInventoryType>(index)))) selectedInventory = static_cast<eInventoryType>(index);
	// 				}
	// 			}

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Setting inventory " +
	// 				GeneralUtils::ASCIIToUTF16(args.at(1)) +
	// 				u" to size " +
	// 				GeneralUtils::to_u16string(size));
	// 		} else ChatPackets::SendSystemMessage(sysAddr, u"Setting inventory ITEMS to size " + GeneralUtils::to_u16string(size));

	// 		auto* inventoryComponent = entity->GetComponent<InventoryComponent>();
	// 		if (inventoryComponent) {
	// 			auto* inventory = inventoryComponent->GetInventory(selectedInventory);

	// 			inventory->SetSize(size);
	// 		}

	// 		return;
	// 	}

	// 	if (chatCommand == "runmacro" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() != 1) return;

	// 		// Only process if input does not contain separator charaters
	// 		if (args[0].find("/") != std::string::npos) return;
	// 		if (args[0].find("\\") != std::string::npos) return;

	// 		auto infile = Game::assetManager->GetFile(("macros/" + args[0] + ".scm").c_str());

	// 		if (!infile) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Unknown macro! Is the filename right?");
	// 			return;
	// 		}

	// 		if (infile.good()) {
	// 			std::string line;
	// 			while (std::getline(infile, line)) {
	// 				// Do this in two separate calls to catch both \n and \r\n
	// 				line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
	// 				line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
	// 				SlashCommandHandler::HandleChatCommand(GeneralUtils::ASCIIToUTF16(line), entity, sysAddr);
	// 			}
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Unknown macro! Is the filename right?");
	// 		}

	// 		return;
	// 	}

	// 	if (chatCommand == "addmission" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() == 0) return;

	// 		const auto missionID = GeneralUtils::TryParse<uint32_t>(args.at(0));

	// 		if (!missionID) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid mission id.");
	// 			return;
	// 		}

	// 		auto comp = static_cast<MissionComponent*>(entity->GetComponent(eReplicaComponentType::MISSION));
	// 		if (comp) comp->AcceptMission(missionID.value(), true);
	// 		return;
	// 	}

	// 	if (chatCommand == "completemission" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() == 0) return;

	// 		const auto missionID = GeneralUtils::TryParse<uint32_t>(args.at(0));

	// 		if (!missionID) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid mission id.");
	// 			return;
	// 		}

	// 		auto comp = static_cast<MissionComponent*>(entity->GetComponent(eReplicaComponentType::MISSION));
	// 		if (comp) comp->CompleteMission(missionID.value(), true);
	// 		return;
	// 	}

	// 	if (chatCommand == "setflag" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() == 1) {
	// 		const auto flagId = GeneralUtils::TryParse<int32_t>(args.at(0));

	// 		if (!flagId) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid flag id.");
	// 			return;
	// 		}

	// 		entity->GetCharacter()->SetPlayerFlag(flagId.value(), true);
	// 	}

	// 	if (chatCommand == "setflag" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() == 2) {
	// 		const auto flagId = GeneralUtils::TryParse<int32_t>(args.at(1));
	// 		std::string onOffFlag = args.at(0);
	// 		if (!flagId) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid flag id.");
	// 			return;
	// 		}
	// 		if (onOffFlag != "off" && onOffFlag != "on") {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid flag type.");
	// 			return;
	// 		}
	// 		entity->GetCharacter()->SetPlayerFlag(flagId.value(), onOffFlag == "on");
	// 	}
	// 	if (chatCommand == "clearflag" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() == 1) {
	// 		const auto flagId = GeneralUtils::TryParse<int32_t>(args.at(0));

	// 		if (!flagId) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid flag id.");
	// 			return;
	// 		}

	// 		entity->GetCharacter()->SetPlayerFlag(flagId.value(), false);
	// 	}

	// 	if (chatCommand == "playeffect" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 3) {
	// 		const auto effectID = GeneralUtils::TryParse<int32_t>(args.at(0));

	// 		if (!effectID) return;

	// 		// FIXME: use fallible ASCIIToUTF16 conversion, because non-ascii isn't valid anyway
	// 		GameMessages::SendPlayFXEffect(entity->GetObjectID(), effectID.value(), GeneralUtils::ASCIIToUTF16(args.at(1)), args.at(2));
	// 	}

	// 	if (chatCommand == "stopeffect" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		GameMessages::SendStopFXEffect(entity, true, args[0]);
	// 	}

	// 	if (chatCommand == "setanntitle" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() < 0) return;

	// 		std::stringstream ss;
	// 		for (auto string : args)
	// 			ss << string << " ";

	// 		entity->GetCharacter()->SetAnnouncementTitle(ss.str());
	// 		return;
	// 	}

	// 	if (chatCommand == "setannmsg" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() < 0) return;

	// 		std::stringstream ss;
	// 		for (auto string : args)
	// 			ss << string << " ";

	// 		entity->GetCharacter()->SetAnnouncementMessage(ss.str());
	// 		return;
	// 	}

	// 	if (chatCommand == "announce" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (entity->GetCharacter()->GetAnnouncementTitle().size() == 0 || entity->GetCharacter()->GetAnnouncementMessage().size() == 0) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Use /setanntitle <title> & /setannmsg <msg> first!");
	// 			return;
	// 		}

	// 		SendAnnouncement(entity->GetCharacter()->GetAnnouncementTitle(), entity->GetCharacter()->GetAnnouncementMessage());
	// 		return;
	// 	}

	// 	if (chatCommand == "shutdownuniverse" && entity->GetGMLevel() == eGameMasterLevel::OPERATOR) {
	// 		//Tell the master server that we're going to be shutting down whole "universe":
	// 		CBITSTREAM;
	// 		BitStreamUtils::WriteHeader(bitStream, eConnectionType::MASTER, eMasterMessageType::SHUTDOWN_UNIVERSE);
	// 		Game::server->SendToMaster(bitStream);
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Sent universe shutdown notification to master.");

	// 		//Tell chat to send an announcement to all servers
	// 		SendAnnouncement("Servers Closing Soon!", "DLU servers will close for maintenance in 10 minutes from now.");
	// 		return;
	// 	}

	// 	if (chatCommand == "getnavmeshheight" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto control = static_cast<ControllablePhysicsComponent*>(entity->GetComponent(eReplicaComponentType::CONTROLLABLE_PHYSICS));
	// 		if (!control) return;

	// 		float y = dpWorld::GetNavMesh()->GetHeightAtPoint(control->GetPosition());
	// 		std::u16string msg = u"Navmesh height: " + (GeneralUtils::to_u16string(y));
	// 		ChatPackets::SendSystemMessage(sysAddr, msg);
	// 	}

	// 	if (chatCommand == "gmadditem" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		if (args.size() == 1) {
	// 			const auto itemLOT = GeneralUtils::TryParse<uint32_t>(args.at(0));

	// 			if (!itemLOT) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid item LOT.");
	// 				return;
	// 			}

	// 			InventoryComponent* inventory = static_cast<InventoryComponent*>(entity->GetComponent(eReplicaComponentType::INVENTORY));

	// 			inventory->AddItem(itemLOT.value(), 1, eLootSourceType::MODERATION);
	// 		} else if (args.size() == 2) {
	// 			const auto itemLOT = GeneralUtils::TryParse<uint32_t>(args.at(0));
	// 			if (!itemLOT) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid item LOT.");
	// 				return;
	// 			}

	// 			const auto count = GeneralUtils::TryParse<uint32_t>(args.at(1));
	// 			if (!count) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid item count.");
	// 				return;
	// 			}

	// 			InventoryComponent* inventory = static_cast<InventoryComponent*>(entity->GetComponent(eReplicaComponentType::INVENTORY));

	// 			inventory->AddItem(itemLOT.value(), count.value(), eLootSourceType::MODERATION);
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Correct usage: /gmadditem <lot>");
	// 		}
	// 	}

	// 	if (chatCommand == "mailitem" && entity->GetGMLevel() >= eGameMasterLevel::MODERATOR && args.size() >= 2) {
	// 		const auto& playerName = args[0];

	// 		auto playerInfo = Database::Get()->GetCharacterInfo(playerName);

	// 		uint32_t receiverID = 0;
	// 		if (!playerInfo) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Failed to find that player");

	// 			return;
	// 		}

	// 		receiverID = playerInfo->id;

	// 		const auto lot = GeneralUtils::TryParse<LOT>(args.at(1));

	// 		if (!lot) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid item lot.");
	// 			return;
	// 		}

	// 		IMail::MailInfo mailInsert;
	// 		mailInsert.senderId = entity->GetObjectID();
	// 		mailInsert.senderUsername = "Darkflame Universe";
	// 		mailInsert.receiverId = receiverID;
	// 		mailInsert.recipient = playerName;
	// 		mailInsert.subject = "Lost item";
	// 		mailInsert.body = "This is a replacement item for one you lost.";
	// 		mailInsert.itemID = LWOOBJID_EMPTY;
	// 		mailInsert.itemLOT = lot.value();
	// 		mailInsert.itemSubkey = LWOOBJID_EMPTY;
	// 		mailInsert.itemCount = 1;
	// 		Database::Get()->InsertNewMail(mailInsert);

	// 		ChatPackets::SendSystemMessage(sysAddr, u"Mail sent");

	// 		return;
	// 	}

	// 	if (chatCommand == "setname" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		std::string name = "";

	// 		for (const auto& arg : args) {
	// 			name += arg + " ";
	// 		}

	// 		GameMessages::SendSetName(entity->GetObjectID(), GeneralUtils::UTF8ToUTF16(name), UNASSIGNED_SYSTEM_ADDRESS);
	// 	}

	// 	if (chatCommand == "title" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		std::string name = entity->GetCharacter()->GetName() + " - ";

	// 		for (const auto& arg : args) {
	// 			name += arg + " ";
	// 		}

	// 		GameMessages::SendSetName(entity->GetObjectID(), GeneralUtils::UTF8ToUTF16(name), UNASSIGNED_SYSTEM_ADDRESS);
	// 	}

	// 	if ((chatCommand == "teleport" || chatCommand == "tele") && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_MODERATOR) {
	// 		NiPoint3 pos{};
	// 		if (args.size() == 3) {

	// 			const auto x = GeneralUtils::TryParse<float>(args.at(0));
	// 			if (!x) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid x.");
	// 				return;
	// 			}

	// 			const auto y = GeneralUtils::TryParse<float>(args.at(1));
	// 			if (!y) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid y.");
	// 				return;
	// 			}

	// 			const auto z = GeneralUtils::TryParse<float>(args.at(2));
	// 			if (!z) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid z.");
	// 				return;
	// 			}

	// 			pos.SetX(x.value());
	// 			pos.SetY(y.value());
	// 			pos.SetZ(z.value());

	// 			LOG("Teleporting objectID: %llu to %f, %f, %f", entity->GetObjectID(), pos.x, pos.y, pos.z);
	// 			GameMessages::SendTeleport(entity->GetObjectID(), pos, NiQuaternion(), sysAddr);
	// 		} else if (args.size() == 2) {

	// 			const auto x = GeneralUtils::TryParse<float>(args.at(0));
	// 			if (!x) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid x.");
	// 				return;
	// 			}

	// 			const auto z = GeneralUtils::TryParse<float>(args.at(1));
	// 			if (!z) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid z.");
	// 				return;
	// 			}

	// 			pos.SetX(x.value());
	// 			pos.SetY(0.0f);
	// 			pos.SetZ(z.value());

	// 			LOG("Teleporting objectID: %llu to X: %f, Z: %f", entity->GetObjectID(), pos.x, pos.z);
	// 			GameMessages::SendTeleport(entity->GetObjectID(), pos, NiQuaternion(), sysAddr);
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Correct usage: /teleport <x> (<y>) <z> - if no Y given, will teleport to the height of the terrain (or any physics object).");
	// 		}


	// 		auto* possessorComponent = entity->GetComponent<PossessorComponent>();
	// 		if (possessorComponent) {
	// 			auto* possassableEntity = Game::entityManager->GetEntity(possessorComponent->GetPossessable());

	// 			if (possassableEntity != nullptr) {
	// 				auto* havokVehiclePhysicsComponent = possassableEntity->GetComponent<HavokVehiclePhysicsComponent>();
	// 				if (havokVehiclePhysicsComponent) {
	// 					havokVehiclePhysicsComponent->SetPosition(pos);
	// 					Game::entityManager->SerializeEntity(possassableEntity);
	// 				} else GameMessages::SendTeleport(possassableEntity->GetObjectID(), pos, NiQuaternion(), sysAddr);
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "tpall" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto pos = entity->GetPosition();

	// 		const auto characters = Game::entityManager->GetEntitiesByComponent(eReplicaComponentType::CHARACTER);

	// 		for (auto* character : characters) {
	// 			GameMessages::SendTeleport(character->GetObjectID(), pos, NiQuaternion(), character->GetSystemAddress());
	// 		}

	// 		return;
	// 	}

	// 	if (chatCommand == "dismount" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* possessorComponent = entity->GetComponent<PossessorComponent>();
	// 		if (possessorComponent) {
	// 			auto possessableId = possessorComponent->GetPossessable();
	// 			if (possessableId != LWOOBJID_EMPTY) {
	// 				auto* possessableEntity = Game::entityManager->GetEntity(possessableId);
	// 				if (possessableEntity) possessorComponent->Dismount(possessableEntity, true);
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "fly" && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_DEVELOPER) {
	// 		auto* character = entity->GetCharacter();

	// 		if (character) {
	// 			bool isFlying = character->GetIsFlying();

	// 			if (isFlying) {
	// 				GameMessages::SendSetJetPackMode(entity, false);

	// 				character->SetIsFlying(false);
	// 			} else {
	// 				float speedScale = 1.0f;

	// 				if (args.size() >= 1) {
	// 					const auto tempScaleStore = GeneralUtils::TryParse<float>(args.at(0));

	// 					if (tempScaleStore) {
	// 						speedScale = tempScaleStore.value();
	// 					} else {
	// 						ChatPackets::SendSystemMessage(sysAddr, u"Failed to parse speed scale argument.");
	// 					}
	// 				}

	// 				float airSpeed = 20 * speedScale;
	// 				float maxAirSpeed = 30 * speedScale;
	// 				float verticalVelocity = 1.5 * speedScale;

	// 				GameMessages::SendSetJetPackMode(entity, true, true, false, 167, airSpeed, maxAirSpeed, verticalVelocity);

	// 				character->SetIsFlying(true);
	// 			}
	// 		}
	// 	}

	// 	//------- GM COMMANDS TO ACTUALLY MODERATE --------

	// 	if (chatCommand == "mute" && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_DEVELOPER) {
	// 		if (args.size() >= 1) {
	// 			auto* player = PlayerManager::GetPlayer(args[0]);

	// 			uint32_t accountId = 0;
	// 			LWOOBJID characterId = 0;

	// 			if (player == nullptr) {
	// 				auto characterInfo = Database::Get()->GetCharacterInfo(args[0]);

	// 				if (characterInfo) {
	// 					accountId = characterInfo->accountId;
	// 					characterId = characterInfo->id;

	// 					GeneralUtils::SetBit(characterId, eObjectBits::CHARACTER);
	// 					GeneralUtils::SetBit(characterId, eObjectBits::PERSISTENT);
	// 				}

	// 				if (accountId == 0) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Count not find player of name: " + GeneralUtils::UTF8ToUTF16(args[0]));

	// 					return;
	// 				}
	// 			} else {
	// 				auto* character = player->GetCharacter();
	// 				auto* user = character != nullptr ? character->GetParentUser() : nullptr;
	// 				if (user) accountId = user->GetAccountID();
	// 				characterId = player->GetObjectID();
	// 			}

	// 			time_t expire = 1; // Default to indefinate mute

	// 			if (args.size() >= 2) {
	// 				const auto days = GeneralUtils::TryParse<uint32_t>(args[1]);
	// 				if (!days) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Invalid days.");

	// 					return;
	// 				}

	// 				std::optional<uint32_t> hours;
	// 				if (args.size() >= 3) {
	// 					hours = GeneralUtils::TryParse<uint32_t>(args[2]);
	// 					if (!hours) {
	// 						ChatPackets::SendSystemMessage(sysAddr, u"Invalid hours.");

	// 						return;
	// 					}
	// 				}

	// 				expire = time(NULL);
	// 				expire += 24 * 60 * 60 * days.value();
	// 				expire += 60 * 60 * hours.value_or(0);
	// 			}

	// 			if (accountId != 0) Database::Get()->UpdateAccountUnmuteTime(accountId, expire);

	// 			char buffer[32] = "brought up for review.\0";

	// 			if (expire != 1) {
	// 				std::tm* ptm = std::localtime(&expire);
	// 				// Format: Mo, 15.06.2009 20:20:00
	// 				std::strftime(buffer, 32, "%a, %d.%m.%Y %H:%M:%S", ptm);
	// 			}

	// 			const auto timeStr = GeneralUtils::ASCIIToUTF16(std::string(buffer));

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Muted: " + GeneralUtils::UTF8ToUTF16(args[0]) + u" until " + timeStr);

	// 			//Notify chat about it
	// 			CBITSTREAM;
	// 			BitStreamUtils::WriteHeader(bitStream, eConnectionType::CHAT_INTERNAL, eChatInternalMessageType::MUTE_UPDATE);

	// 			bitStream.Write(characterId);
	// 			bitStream.Write(expire);

	// 			Game::chatServer->Send(&bitStream, SYSTEM_PRIORITY, RELIABLE, 0, Game::chatSysAddr, false);
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Correct usage: /mute <username> <days (optional)> <hours (optional)>");
	// 		}
	// 	}

	// 	if (chatCommand == "kick" && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_MODERATOR) {
	// 		if (args.size() == 1) {
	// 			auto* player = PlayerManager::GetPlayer(args[0]);

	// 			std::u16string username = GeneralUtils::UTF8ToUTF16(args[0]);
	// 			if (player == nullptr) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Count not find player of name: " + username);
	// 				return;
	// 			}

	// 			Game::server->Disconnect(player->GetSystemAddress(), eServerDisconnectIdentifiers::KICK);

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Kicked: " + username);
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Correct usage: /kick <username>");
	// 		}
	// 	}

	// 	if (chatCommand == "ban" && entity->GetGMLevel() >= eGameMasterLevel::SENIOR_MODERATOR) {
	// 		if (args.size() == 1) {
	// 			auto* player = PlayerManager::GetPlayer(args[0]);

	// 			uint32_t accountId = 0;

	// 			if (player == nullptr) {
	// 				auto characterInfo = Database::Get()->GetCharacterInfo(args[0]);

	// 				if (characterInfo) {
	// 					accountId = characterInfo->accountId;
	// 				}

	// 				if (accountId == 0) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Count not find player of name: " + GeneralUtils::UTF8ToUTF16(args[0]));

	// 					return;
	// 				}
	// 			} else {
	// 				auto* character = player->GetCharacter();
	// 				auto* user = character != nullptr ? character->GetParentUser() : nullptr;
	// 				if (user) accountId = user->GetAccountID();
	// 			}

	// 			if (accountId != 0) Database::Get()->UpdateAccountBan(accountId, true);

	// 			if (player != nullptr) {
	// 				Game::server->Disconnect(player->GetSystemAddress(), eServerDisconnectIdentifiers::FREE_TRIAL_EXPIRED);
	// 			}

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Banned: " + GeneralUtils::ASCIIToUTF16(args[0]));
	// 		} else {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Correct usage: /ban <username>");
	// 		}
	// 	}

	// 	//-------------------------------------------------

	// 	if (chatCommand == "buffme" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto dest = static_cast<DestroyableComponent*>(entity->GetComponent(eReplicaComponentType::DESTROYABLE));
	// 		if (dest) {
	// 			dest->SetHealth(999);
	// 			dest->SetMaxHealth(999.0f);
	// 			dest->SetArmor(999);
	// 			dest->SetMaxArmor(999.0f);
	// 			dest->SetImagination(999);
	// 			dest->SetMaxImagination(999.0f);
	// 		}
	// 		Game::entityManager->SerializeEntity(entity);
	// 	}

	// 	if (chatCommand == "startcelebration" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() == 1) {
	// 		const auto celebration = GeneralUtils::TryParse<int32_t>(args.at(0));

	// 		if (!celebration) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid celebration.");
	// 			return;
	// 		}

	// 		GameMessages::SendStartCelebrationEffect(entity, entity->GetSystemAddress(), celebration.value());
	// 	}

	// 	if (chatCommand == "buffmed" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto dest = static_cast<DestroyableComponent*>(entity->GetComponent(eReplicaComponentType::DESTROYABLE));
	// 		if (dest) {
	// 			dest->SetHealth(9);
	// 			dest->SetMaxHealth(9.0f);
	// 			dest->SetArmor(9);
	// 			dest->SetMaxArmor(9.0f);
	// 			dest->SetImagination(9);
	// 			dest->SetMaxImagination(9.0f);
	// 		}
	// 		Game::entityManager->SerializeEntity(entity);
	// 	}

	// 	if (chatCommand == "refillstats" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {

	// 		auto dest = static_cast<DestroyableComponent*>(entity->GetComponent(eReplicaComponentType::DESTROYABLE));
	// 		if (dest) {
	// 			dest->SetHealth(static_cast<int32_t>(dest->GetMaxHealth()));
	// 			dest->SetArmor(static_cast<int32_t>(dest->GetMaxArmor()));
	// 			dest->SetImagination(static_cast<int32_t>(dest->GetMaxImagination()));
	// 		}

	// 		Game::entityManager->SerializeEntity(entity);
	// 	}

	// 	if (chatCommand == "lookup" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto query = CDClientDatabase::CreatePreppedStmt(
	// 			"SELECT `id`, `name` FROM `Objects` WHERE `displayName` LIKE ?1 OR `name` LIKE ?1 OR `description` LIKE ?1 LIMIT 50");
	// 		// Concatenate all of the arguments into a single query so a multi word query can be used properly.
	// 		std::string conditional = args[0];
	// 		args.erase(args.begin());
	// 		for (auto& argument : args) {
	// 			conditional += ' ' + argument;
	// 		}

	// 		const std::string query_text = "%" + conditional + "%";
	// 		query.bind(1, query_text.c_str());

	// 		auto tables = query.execQuery();

	// 		while (!tables.eof()) {
	// 			std::string message = std::to_string(tables.getIntField(0)) + " - " + tables.getStringField(1);
	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::UTF8ToUTF16(message, message.size()));
	// 			tables.nextRow();
	// 		}
	// 	}

	// 	if (chatCommand == "spawn" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		ControllablePhysicsComponent* comp = static_cast<ControllablePhysicsComponent*>(entity->GetComponent(eReplicaComponentType::CONTROLLABLE_PHYSICS));
	// 		if (!comp) return;

	// 		const auto lot = GeneralUtils::TryParse<uint32_t>(args[0]);

	// 		if (!lot) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid lot.");
	// 			return;
	// 		}

	// 		EntityInfo info;
	// 		info.lot = lot.value();
	// 		info.pos = comp->GetPosition();
	// 		info.rot = comp->GetRotation();
	// 		info.spawner = nullptr;
	// 		info.spawnerID = entity->GetObjectID();
	// 		info.spawnerNodeID = 0;

	// 		Entity* newEntity = Game::entityManager->CreateEntity(info, nullptr);

	// 		if (newEntity == nullptr) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Failed to spawn entity.");
	// 			return;
	// 		}

	// 		Game::entityManager->ConstructEntity(newEntity);
	// 	}

	// 	if (chatCommand == "spawngroup" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 3) {
	// 		auto controllablePhysicsComponent = entity->GetComponent<ControllablePhysicsComponent>();
	// 		if (!controllablePhysicsComponent) return;

	// 		const auto lot = GeneralUtils::TryParse<LOT>(args[0]);
	// 		if (!lot) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid lot.");
	// 			return;
	// 		}

	// 		const auto numberToSpawnOptional = GeneralUtils::TryParse<uint32_t>(args[1]);
	// 		if (!numberToSpawnOptional && numberToSpawnOptional.value() > 0) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid number of enemies to spawn.");
	// 			return;
	// 		}
	// 		uint32_t numberToSpawn = numberToSpawnOptional.value();

	// 		// Must spawn within a radius of at least 0.0f
	// 		const auto radiusToSpawnWithinOptional = GeneralUtils::TryParse<float>(args[2]);
	// 		if (!radiusToSpawnWithinOptional && radiusToSpawnWithinOptional.value() < 0.0f) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid radius to spawn within.");
	// 			return;
	// 		}
	// 		const float radiusToSpawnWithin = radiusToSpawnWithinOptional.value();

	// 		EntityInfo info;
	// 		info.lot = lot.value();
	// 		info.spawner = nullptr;
	// 		info.spawnerID = entity->GetObjectID();
	// 		info.spawnerNodeID = 0;

	// 		auto playerPosition = controllablePhysicsComponent->GetPosition();
	// 		while (numberToSpawn > 0) {
	// 			auto randomAngle = GeneralUtils::GenerateRandomNumber<float>(0.0f, 2 * PI);
	// 			auto randomRadius = GeneralUtils::GenerateRandomNumber<float>(0.0f, radiusToSpawnWithin);

	// 			// Set the position to the generated random position plus the player position.  This will
	// 			// spawn the entity in a circle around the player.  As you get further from the player, the angle chosen will get less accurate.
	// 			info.pos = playerPosition + NiPoint3(cos(randomAngle) * randomRadius, 0.0f, sin(randomAngle) * randomRadius);
	// 			info.rot = NiQuaternion();

	// 			auto newEntity = Game::entityManager->CreateEntity(info);
	// 			if (newEntity == nullptr) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Failed to spawn entity.");
	// 				return;
	// 			}

	// 			Game::entityManager->ConstructEntity(newEntity);
	// 			numberToSpawn--;
	// 		}
	// 	}

	// 	if ((chatCommand == "giveuscore") && args.size() >= 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto uscoreOptional = GeneralUtils::TryParse<int32_t>(args[0]);
	// 		if (!uscoreOptional) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid uscore.");
	// 			return;
	// 		}
	// 		const int32_t uscore = uscoreOptional.value();

	// 		CharacterComponent* character = entity->GetComponent<CharacterComponent>();
	// 		if (character) character->SetUScore(character->GetUScore() + uscore);
	// 		// MODERATION should work but it doesn't.  Relog to see uscore changes

	// 		eLootSourceType lootType = eLootSourceType::MODERATION;

	// 		if (args.size() >= 2) {
	// 			const auto type = GeneralUtils::TryParse<eLootSourceType>(args[1]);
	// 			lootType = type.value_or(lootType);
	// 		}

	// 		GameMessages::SendModifyLEGOScore(entity, entity->GetSystemAddress(), uscore, lootType);
	// 	}

	// 	if ((chatCommand == "setlevel") && args.size() >= 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		// We may be trying to set a specific players level to a level.  If so override the entity with the requested players.
	// 		std::string requestedPlayerToSetLevelOf = "";
	// 		if (args.size() > 1) {
	// 			requestedPlayerToSetLevelOf = args[1];

	// 			auto requestedPlayer = PlayerManager::GetPlayer(requestedPlayerToSetLevelOf);

	// 			if (!requestedPlayer) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"No player found with username: (" + GeneralUtils::UTF8ToUTF16(requestedPlayerToSetLevelOf) + u").");
	// 				return;
	// 			}

	// 			if (!requestedPlayer->GetOwner()) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"No entity found with username: (" + GeneralUtils::UTF8ToUTF16(requestedPlayerToSetLevelOf) + u").");
	// 				return;
	// 			}

	// 			entity = requestedPlayer->GetOwner();
	// 		}
	// 		const auto requestedLevelOptional = GeneralUtils::TryParse<uint32_t>(args[0]);
	// 		uint32_t oldLevel;

	// 		// first check the level is valid
	// 		if (!requestedLevelOptional) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid level.");
	// 			return;
	// 		}
	// 		uint32_t requestedLevel = requestedLevelOptional.value();
	// 		// query to set our uscore to the correct value for this level

	// 		auto characterComponent = entity->GetComponent<CharacterComponent>();
	// 		if (!characterComponent) return;
	// 		auto levelComponent = entity->GetComponent<LevelProgressionComponent>();
	// 		auto query = CDClientDatabase::CreatePreppedStmt("SELECT requiredUScore from LevelProgressionLookup WHERE id = ?;");
	// 		query.bind(1, static_cast<int>(requestedLevel));
	// 		auto result = query.execQuery();

	// 		if (result.eof()) return;

	// 		// Set the UScore first
	// 		oldLevel = levelComponent->GetLevel();
	// 		characterComponent->SetUScore(result.getIntField(0, characterComponent->GetUScore()));

	// 		// handle level up for each level we have passed if we set our level to be higher than the current one.
	// 		if (oldLevel < requestedLevel) {
	// 			while (oldLevel < requestedLevel) {
	// 				oldLevel += 1;
	// 				levelComponent->SetLevel(oldLevel);
	// 				levelComponent->HandleLevelUp();
	// 			}
	// 		} else {
	// 			levelComponent->SetLevel(requestedLevel);
	// 		}

	// 		if (requestedPlayerToSetLevelOf != "") {
	// 			ChatPackets::SendSystemMessage(
	// 				sysAddr, u"Set " + GeneralUtils::UTF8ToUTF16(requestedPlayerToSetLevelOf) + u"'s level to " + GeneralUtils::to_u16string(requestedLevel) +
	// 				u" and UScore to " + GeneralUtils::to_u16string(characterComponent->GetUScore()) +
	// 				u". Relog to see changes.");
	// 		} else {
	// 			ChatPackets::SendSystemMessage(
	// 				sysAddr, u"Set your level to " + GeneralUtils::to_u16string(requestedLevel) +
	// 				u" and UScore to " + GeneralUtils::to_u16string(characterComponent->GetUScore()) +
	// 				u". Relog to see changes.");
	// 		}
	// 		return;
	// 	}

	// 	if (chatCommand == "pos" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto position = entity->GetPosition();

	// 		ChatPackets::SendSystemMessage(sysAddr, u"<" + (GeneralUtils::to_u16string(position.x)) + u", " + (GeneralUtils::to_u16string(position.y)) + u", " + (GeneralUtils::to_u16string(position.z)) + u">");

	// 		LOG("Position: %f, %f, %f", position.x, position.y, position.z);
	// 	}

	// 	if (chatCommand == "rot" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto rotation = entity->GetRotation();

	// 		ChatPackets::SendSystemMessage(sysAddr, u"<" + (GeneralUtils::to_u16string(rotation.w)) + u", " + (GeneralUtils::to_u16string(rotation.x)) + u", " + (GeneralUtils::to_u16string(rotation.y)) + u", " + (GeneralUtils::to_u16string(rotation.z)) + u">");

	// 		LOG("Rotation: %f, %f, %f, %f", rotation.w, rotation.x, rotation.y, rotation.z);
	// 	}

	// 	if (chatCommand == "locrow" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto position = entity->GetPosition();
	// 		const auto rotation = entity->GetRotation();

	// 		LOG("<location x=\"%f\" y=\"%f\" z=\"%f\" rw=\"%f\" rx=\"%f\" ry=\"%f\" rz=\"%f\" />", position.x, position.y, position.z, rotation.w, rotation.x, rotation.y, rotation.z);
	// 	}

	// 	if (chatCommand == "playlvlfx" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		GameMessages::SendPlayFXEffect(entity, 7074, u"create", "7074", LWOOBJID_EMPTY, 1.0f, 1.0f, true);
	// 	}

	// 	if (chatCommand == "playrebuildfx" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		GameMessages::SendPlayFXEffect(entity, 230, u"rebuild", "230", LWOOBJID_EMPTY, 1.0f, 1.0f, true);
	// 	}

	// 	if ((chatCommand == "freemoney" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) && args.size() == 1) {
	// 		const auto money = GeneralUtils::TryParse<int64_t>(args[0]);

	// 		if (!money) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid money.");
	// 			return;
	// 		}

	// 		auto* ch = entity->GetCharacter();
	// 		ch->SetCoins(ch->GetCoins() + money.value(), eLootSourceType::MODERATION);
	// 	}

	// 	if ((chatCommand == "setcurrency") && args.size() == 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		const auto money = GeneralUtils::TryParse<int64_t>(args[0]);

	// 		if (!money) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid money.");
	// 			return;
	// 		}

	// 		auto* ch = entity->GetCharacter();
	// 		ch->SetCoins(money.value(), eLootSourceType::MODERATION);
	// 	}

	// 	// Allow for this on even while not a GM, as it sometimes toggles incorrrectly.
	// 	if (chatCommand == "gminvis" && entity->GetCharacter()->GetParentUser()->GetMaxGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		GameMessages::SendToggleGMInvis(entity->GetObjectID(), true, UNASSIGNED_SYSTEM_ADDRESS);

	// 		return;
	// 	}

	// 	if (chatCommand == "gmimmune" && args.size() >= 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();

	// 		const auto state = GeneralUtils::TryParse<int32_t>(args[0]);

	// 		if (!state) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid state.");
	// 			return;
	// 		}

	// 		if (destroyableComponent) destroyableComponent->SetIsGMImmune(state.value());
	// 		return;
	// 	}

	// 	//Testing basic attack immunity
	// 	if (chatCommand == "attackimmune" && args.size() >= 1 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();

	// 		const auto state = GeneralUtils::TryParse<int32_t>(args[0]);

	// 		if (!state) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid state.");
	// 			return;
	// 		}

	// 		if (destroyableComponent) destroyableComponent->SetIsImmune(state.value());
	// 		return;
	// 	}

	// 	if (chatCommand == "buff" && args.size() >= 2 && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* buffComponent = entity->GetComponent<BuffComponent>();

	// 		const auto id = GeneralUtils::TryParse<int32_t>(args[0]);
	// 		if (!id) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid buff id.");
	// 			return;
	// 		}

	// 		const auto duration = GeneralUtils::TryParse<int32_t>(args[1]);
	// 		if (!duration) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid buff duration.");
	// 			return;
	// 		}

	// 		if (buffComponent) buffComponent->ApplyBuff(id.value(), duration.value(), entity->GetObjectID());
	// 		return;
	// 	}

	// 	if ((chatCommand == "testmap" && args.size() >= 1) && entity->GetGMLevel() >= eGameMasterLevel::FORUM_MODERATOR) {
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Requesting map change...");
	// 		LWOCLONEID cloneId = 0;
	// 		bool force = false;

	// 		const auto reqZoneOptional = GeneralUtils::TryParse<LWOMAPID>(args[0]);
	// 		if (!reqZoneOptional) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid zone.");
	// 			return;
	// 		}
	// 		const LWOMAPID reqZone = reqZoneOptional.value();

	// 		if (args.size() > 1) {
	// 			auto index = 1;

	// 			if (args[index] == "force") {
	// 				index++;

	// 				force = true;
	// 			}

	// 			if (args.size() > index) {
	// 				const auto cloneIdOptional = GeneralUtils::TryParse<LWOCLONEID>(args[index]);
	// 				if (!cloneIdOptional) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Invalid clone id.");
	// 					return;
	// 				}
	// 				cloneId = cloneIdOptional.value();
	// 			}
	// 		}

	// 		const auto objid = entity->GetObjectID();

	// 		if (force || Game::zoneManager->CheckIfAccessibleZone(reqZone)) { // to prevent tomfoolery

	// 			ZoneInstanceManager::Instance()->RequestZoneTransfer(Game::server, reqZone, cloneId, false, [objid](bool mythranShift, uint32_t zoneID, uint32_t zoneInstance, uint32_t zoneClone, std::string serverIP, uint16_t serverPort) {

	// 				auto* entity = Game::entityManager->GetEntity(objid);
	// 				if (!entity) return;

	// 				const auto sysAddr = entity->GetSystemAddress();

	// 				ChatPackets::SendSystemMessage(sysAddr, u"Transfering map...");

	// 				LOG("Transferring %s to Zone %i (Instance %i | Clone %i | Mythran Shift: %s) with IP %s and Port %i", sysAddr.ToString(), zoneID, zoneInstance, zoneClone, mythranShift == true ? "true" : "false", serverIP.c_str(), serverPort);
	// 				if (entity->GetCharacter()) {
	// 					entity->GetCharacter()->SetZoneID(zoneID);
	// 					entity->GetCharacter()->SetZoneInstance(zoneInstance);
	// 					entity->GetCharacter()->SetZoneClone(zoneClone);
	// 					entity->GetComponent<CharacterComponent>()->SetLastRocketConfig(u"");
	// 				}

	// 				entity->GetCharacter()->SaveXMLToDatabase();

	// 				WorldPackets::SendTransferToWorld(sysAddr, serverIP, serverPort, mythranShift);
	// 				return;
	// 				});
	// 		} else {
	// 			std::string msg = "ZoneID not found or allowed: ";
	// 			msg.append(args[0]); // FIXME: unnecessary utf16 re-encoding just for error
	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::UTF8ToUTF16(msg, msg.size()));
	// 		}
	// 	}

	// 	if (chatCommand == "createprivate" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 3) {
	// 		const auto zone = GeneralUtils::TryParse<uint32_t>(args[0]);
	// 		if (!zone) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid zone.");
	// 			return;
	// 		}

	// 		const auto clone = GeneralUtils::TryParse<uint32_t>(args[1]);
	// 		if (!clone) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid clone.");
	// 			return;
	// 		}

	// 		const auto& password = args[2];

	// 		ZoneInstanceManager::Instance()->CreatePrivateZone(Game::server, zone.value(), clone.value(), password);

	// 		ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16("Sent request for private zone with password: " + password));

	// 		return;
	// 	}

	// 	if ((chatCommand == "debugui") && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Opening UIDebugger...");
	// 		AMFArrayValue args;
	// 		GameMessages::SendUIMessageServerToSingleClient(entity, sysAddr, "ToggleUIDebugger;", args);
	// 	}

	// 	if ((chatCommand == "boost") && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* possessorComponent = entity->GetComponent<PossessorComponent>();

	// 		if (possessorComponent == nullptr) {
	// 			return;
	// 		}

	// 		auto* vehicle = Game::entityManager->GetEntity(possessorComponent->GetPossessable());

	// 		if (vehicle == nullptr) {
	// 			return;
	// 		}

	// 		if (args.size() >= 1) {
	// 			const auto time = GeneralUtils::TryParse<float>(args[0]);

	// 			if (!time) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Invalid boost time.");
	// 				return;
	// 			} else {
	// 				GameMessages::SendVehicleAddPassiveBoostAction(vehicle->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS);
	// 				entity->AddCallbackTimer(time.value(), [vehicle]() {
	// 					if (!vehicle) return;
	// 					GameMessages::SendVehicleRemovePassiveBoostAction(vehicle->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS);
	// 					});
	// 			}
	// 		} else {
	// 			GameMessages::SendVehicleAddPassiveBoostAction(vehicle->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS);
	// 		}

	// 	}

	// 	if ((chatCommand == "unboost") && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* possessorComponent = entity->GetComponent<PossessorComponent>();

	// 		if (possessorComponent == nullptr) return;
	// 		auto* vehicle = Game::entityManager->GetEntity(possessorComponent->GetPossessable());

	// 		if (vehicle == nullptr) return;
	// 		GameMessages::SendVehicleRemovePassiveBoostAction(vehicle->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS);
	// 	}

	// 	if (chatCommand == "activatespawner" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto spawners = Game::zoneManager->GetSpawnersByName(args[0]);

	// 		for (auto* spawner : spawners) {
	// 			spawner->Activate();
	// 		}

	// 		spawners = Game::zoneManager->GetSpawnersInGroup(args[0]);

	// 		for (auto* spawner : spawners) {
	// 			spawner->Activate();
	// 		}
	// 	}

	// 	if (chatCommand == "spawnphysicsverts" && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_DEVELOPER) {
	// 		//Go tell physics to spawn all the vertices:
	// 		auto entities = Game::entityManager->GetEntitiesByComponent(eReplicaComponentType::PHANTOM_PHYSICS);
	// 		for (auto en : entities) {
	// 			auto phys = static_cast<PhantomPhysicsComponent*>(en->GetComponent(eReplicaComponentType::PHANTOM_PHYSICS));
	// 			if (phys)
	// 				phys->SpawnVertices();
	// 		}
	// 	}

	// 	if (chatCommand == "reportproxphys" && entity->GetGMLevel() >= eGameMasterLevel::JUNIOR_DEVELOPER) {
	// 		auto entities = Game::entityManager->GetEntitiesByComponent(eReplicaComponentType::PROXIMITY_MONITOR);
	// 		for (auto en : entities) {
	// 			auto phys = static_cast<ProximityMonitorComponent*>(en->GetComponent(eReplicaComponentType::PROXIMITY_MONITOR));
	// 			if (phys) {
	// 				for (auto prox : phys->GetProximitiesData()) {
	// 					if (!prox.second) continue;

	// 					auto sphere = static_cast<dpShapeSphere*>(prox.second->GetShape());
	// 					auto pos = prox.second->GetPosition();
	// 					LOG("Proximity: %s, r: %f, pos: %f, %f, %f", prox.first.c_str(), sphere->GetRadius(), pos.x, pos.y, pos.z);
	// 				}
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "triggerspawner" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto spawners = Game::zoneManager->GetSpawnersByName(args[0]);

	// 		for (auto* spawner : spawners) {
	// 			spawner->Spawn();
	// 		}

	// 		spawners = Game::zoneManager->GetSpawnersInGroup(args[0]);

	// 		for (auto* spawner : spawners) {
	// 			spawner->Spawn();
	// 		}
	// 	}

	// 	if (chatCommand == "reforge" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 2) {
	// 		const auto baseItem = GeneralUtils::TryParse<LOT>(args[0]);
	// 		if (!baseItem) return;

	// 		const auto reforgedItem = GeneralUtils::TryParse<LOT>(args[1]);
	// 		if (!reforgedItem) return;

	// 		auto* inventoryComponent = entity->GetComponent<InventoryComponent>();
	// 		if (!inventoryComponent) return;

	// 		std::vector<LDFBaseData*> data{};
	// 		data.push_back(new LDFData<int32_t>(u"reforgedLOT", reforgedItem.value()));

	// 		inventoryComponent->AddItem(baseItem.value(), 1, eLootSourceType::MODERATION, eInventoryType::INVALID, data);
	// 	}

	// 	if (chatCommand == "crash" && entity->GetGMLevel() >= eGameMasterLevel::OPERATOR) {
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Crashing...");

	// 		int* badPtr = nullptr;
	// 		*badPtr = 0;

	// 		return;
	// 	}

	// 	if (chatCommand == "metrics" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		for (const auto variable : Metrics::GetAllMetrics()) {
	// 			auto* metric = Metrics::GetMetric(variable);

	// 			if (metric == nullptr) {
	// 				continue;
	// 			}

	// 			ChatPackets::SendSystemMessage(
	// 				sysAddr,
	// 				GeneralUtils::ASCIIToUTF16(Metrics::MetricVariableToString(variable)) +
	// 				u": " +
	// 				GeneralUtils::to_u16string(Metrics::ToMiliseconds(metric->average)) +
	// 				u"ms"
	// 			);
	// 		}

	// 		ChatPackets::SendSystemMessage(
	// 			sysAddr,
	// 			u"Peak RSS: " + GeneralUtils::to_u16string(static_cast<float>(static_cast<double>(Metrics::GetPeakRSS()) / 1.024e6)) +
	// 			u"MB"
	// 		);

	// 		ChatPackets::SendSystemMessage(
	// 			sysAddr,
	// 			u"Current RSS: " + GeneralUtils::to_u16string(static_cast<float>(static_cast<double>(Metrics::GetCurrentRSS()) / 1.024e6)) +
	// 			u"MB"
	// 		);

	// 		ChatPackets::SendSystemMessage(
	// 			sysAddr,
	// 			u"Process ID: " + GeneralUtils::to_u16string(Metrics::GetProcessID())
	// 		);

	// 		return;
	// 	}

	// 	if (chatCommand == "reloadconfig" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		Game::config->ReloadConfig();
	// 		VanityUtilities::SpawnVanity();
	// 		dpWorld::Reload();
	// 		auto entities = Game::entityManager->GetEntitiesByComponent(eReplicaComponentType::SCRIPTED_ACTIVITY);
	// 		for (const auto* const entity : entities) {
	// 			auto* const scriptedActivityComponent = entity->GetComponent<ScriptedActivityComponent>();
	// 			if (!scriptedActivityComponent) continue;

	// 			scriptedActivityComponent->ReloadConfig();
	// 		}
	// 		Game::server->UpdateMaximumMtuSize();
	// 		Game::server->UpdateBandwidthLimit();
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Successfully reloaded config for world!");
	// 	}

	// 	if (chatCommand == "rollloot" && entity->GetGMLevel() >= eGameMasterLevel::OPERATOR && args.size() >= 3) {
	// 		const auto lootMatrixIndex = GeneralUtils::TryParse<uint32_t>(args[0]);
	// 		if (!lootMatrixIndex) return;

	// 		const auto targetLot = GeneralUtils::TryParse<uint32_t>(args[1]);
	// 		if (!targetLot) return;

	// 		const auto loops = GeneralUtils::TryParse<uint32_t>(args[2]);
	// 		if (!loops) return;

	// 		uint64_t totalRuns = 0;

	// 		for (uint32_t i = 0; i < loops; i++) {
	// 			while (true) {
	// 				auto lootRoll = Loot::RollLootMatrix(lootMatrixIndex.value());
	// 				totalRuns += 1;
	// 				bool doBreak = false;
	// 				for (const auto& kv : lootRoll) {
	// 					if (static_cast<uint32_t>(kv.first) == targetLot) {
	// 						doBreak = true;
	// 					}
	// 				}
	// 				if (doBreak) break;
	// 			}
	// 		}

	// 		std::u16string message = u"Ran loot drops looking for "
	// 			+ GeneralUtils::to_u16string(targetLot.value())
	// 			+ u", "
	// 			+ GeneralUtils::to_u16string(loops.value())
	// 			+ u" times. It ran "
	// 			+ GeneralUtils::to_u16string(totalRuns)
	// 			+ u" times. Averaging out at "
	// 			+ GeneralUtils::to_u16string(static_cast<float>(totalRuns) / loops.value());

	// 		ChatPackets::SendSystemMessage(sysAddr, message);
	// 	}

	// 	if (chatCommand == "deleteinven" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		eInventoryType inventoryType = eInventoryType::INVALID;

	// 		const auto inventoryTypeOptional = GeneralUtils::TryParse<eInventoryType>(args[0]);
	// 		if (!inventoryTypeOptional) {
	// 			// In this case, we treat the input as a string and try to find it in the reflection list
	// 			std::transform(args[0].begin(), args[0].end(), args[0].begin(), ::toupper);
	// 			LOG("looking for inventory %s", args[0].c_str());
	// 			for (uint32_t index = 0; index < NUMBER_OF_INVENTORIES; index++) {
	// 				if (std::string_view(args[0]) == std::string_view(InventoryType::InventoryTypeToString(static_cast<eInventoryType>(index)))) inventoryType = static_cast<eInventoryType>(index);
	// 			}
	// 		} else {
	// 			inventoryType = inventoryTypeOptional.value();
	// 		}

	// 		if (inventoryType == eInventoryType::INVALID || inventoryType >= NUMBER_OF_INVENTORIES) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Invalid inventory provided.");
	// 			return;
	// 		}

	// 		auto* inventoryComponent = entity->GetComponent<InventoryComponent>();
	// 		if (!inventoryComponent) return;

	// 		auto* inventoryToDelete = inventoryComponent->GetInventory(inventoryType);
	// 		if (!inventoryToDelete) return;

	// 		inventoryToDelete->DeleteAllItems();
	// 		LOG("Deleted inventory %s for user %llu", args[0].c_str(), entity->GetObjectID());
	// 		ChatPackets::SendSystemMessage(sysAddr, u"Deleted inventory " + GeneralUtils::UTF8ToUTF16(args[0]));
	// 	}

	// 	if (chatCommand == "castskill" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto* skillComponent = entity->GetComponent<SkillComponent>();
	// 		if (skillComponent) {
	// 			const auto skillId = GeneralUtils::TryParse<uint32_t>(args[0]);

	// 			if (!skillId) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Error getting skill ID.");
	// 				return;
	// 			} else {
	// 				skillComponent->CastSkill(skillId.value(), entity->GetObjectID(), entity->GetObjectID());
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Cast skill");
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "setskillslot" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 2) {
	// 		auto* const inventoryComponent = entity->GetComponent<InventoryComponent>();
	// 		if (inventoryComponent) {
	// 			const auto slot = GeneralUtils::TryParse<BehaviorSlot>(args[0]);
	// 			if (!slot) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Error getting slot.");
	// 				return;
	// 			} else {
	// 				const auto skillId = GeneralUtils::TryParse<uint32_t>(args[1]);
	// 				if (!skillId) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Error getting skill.");
	// 					return;
	// 				} else {
	// 					if (inventoryComponent->SetSkill(slot.value(), skillId.value())) ChatPackets::SendSystemMessage(sysAddr, u"Set skill to slot successfully");
	// 					else ChatPackets::SendSystemMessage(sysAddr, u"Set skill to slot failed");
	// 				}
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "setfaction" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();
	// 		if (destroyableComponent) {
	// 			const auto faction = GeneralUtils::TryParse<int32_t>(args[0]);

	// 			if (!faction) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Error getting faction.");
	// 				return;
	// 			} else {
	// 				destroyableComponent->SetFaction(faction.value());
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Set faction and updated enemies list");
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "addfaction" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();
	// 		if (destroyableComponent) {
	// 			const auto faction = GeneralUtils::TryParse<int32_t>(args[0]);

	// 			if (!faction) {
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Error getting faction.");
	// 				return;
	// 			} else {
	// 				destroyableComponent->AddFaction(faction.value());
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Added faction and updated enemies list");
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "getfactions" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER) {
	// 		auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();
	// 		if (destroyableComponent) {
	// 			ChatPackets::SendSystemMessage(sysAddr, u"Friendly factions:");
	// 			for (const auto entry : destroyableComponent->GetFactionIDs()) {
	// 				ChatPackets::SendSystemMessage(sysAddr, (GeneralUtils::to_u16string(entry)));
	// 			}

	// 			ChatPackets::SendSystemMessage(sysAddr, u"Enemy factions:");
	// 			for (const auto entry : destroyableComponent->GetEnemyFactionsIDs()) {
	// 				ChatPackets::SendSystemMessage(sysAddr, (GeneralUtils::to_u16string(entry)));
	// 			}
	// 		}
	// 	}

	// 	if (chatCommand == "setrewardcode" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() == 1) {
	// 		auto* cdrewardCodes = CDClientManager::GetTable<CDRewardCodesTable>();

	// 		auto id = cdrewardCodes->GetCodeID(args[0]);
	// 		if (id != -1) Database::Get()->InsertRewardCode(user->GetAccountID(), id);
	// 	}

	// 	if (chatCommand == "inspect" && entity->GetGMLevel() >= eGameMasterLevel::DEVELOPER && args.size() >= 1) {
	// 		Entity* closest = nullptr;

	// 		std::u16string ldf;

	// 		bool isLDF = false;

	// 		auto component = GeneralUtils::TryParse<eReplicaComponentType>(args[0]);
	// 		if (!component) {
	// 			component = eReplicaComponentType::INVALID;

	// 			ldf = GeneralUtils::UTF8ToUTF16(args[0]);

	// 			isLDF = true;
	// 		}

	// 		auto reference = entity->GetPosition();

	// 		auto closestDistance = 0.0f;

	// 		const auto candidates = Game::entityManager->GetEntitiesByComponent(component.value());

	// 		for (auto* candidate : candidates) {
	// 			if (candidate->GetLOT() == 1 || candidate->GetLOT() == 8092) {
	// 				continue;
	// 			}

	// 			if (isLDF && !candidate->HasVar(ldf)) {
	// 				continue;
	// 			}

	// 			if (!closest) {
	// 				closest = candidate;

	// 				closestDistance = NiPoint3::Distance(candidate->GetPosition(), reference);

	// 				continue;
	// 			}

	// 			const auto distance = NiPoint3::Distance(candidate->GetPosition(), reference);

	// 			if (distance < closestDistance) {
	// 				closest = candidate;

	// 				closestDistance = distance;
	// 			}
	// 		}

	// 		if (!closest) return;

	// 		Game::entityManager->SerializeEntity(closest);

	// 		auto* table = CDClientManager::GetTable<CDObjectsTable>();

	// 		const auto& info = table->GetByID(closest->GetLOT());

	// 		std::stringstream header;

	// 		header << info.name << " [" << std::to_string(info.id) << "]" << " " << std::to_string(closestDistance) << " " << std::to_string(closest->IsSleeping());

	// 		ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(header.str()));

	// 		for (const auto& pair : closest->GetComponents()) {
	// 			auto id = pair.first;

	// 			std::stringstream stream;

	// 			stream << "Component [" << std::to_string(static_cast<uint32_t>(id)) << "]";

	// 			ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::ASCIIToUTF16(stream.str()));
	// 		}

	// 		if (args.size() >= 2) {
	// 			if (args[1] == "-m" && args.size() >= 3) {
	// 				auto* const movingPlatformComponent = closest->GetComponent<MovingPlatformComponent>();

	// 				const auto mValue = GeneralUtils::TryParse<int32_t>(args[2]);

	// 				if (!movingPlatformComponent || !mValue) return;

	// 				movingPlatformComponent->SetSerialized(true);

	// 				if (mValue == -1) {
	// 					movingPlatformComponent->StopPathing();
	// 				} else {
	// 					movingPlatformComponent->GotoWaypoint(mValue.value());
	// 				}

	// 				Game::entityManager->SerializeEntity(closest);
	// 			} else if (args[1] == "-a" && args.size() >= 3) {
	// 				RenderComponent::PlayAnimation(closest, args.at(2));
	// 			} else if (args[1] == "-s") {
	// 				for (auto* entry : closest->GetSettings()) {
	// 					ChatPackets::SendSystemMessage(sysAddr, GeneralUtils::UTF8ToUTF16(entry->GetString()));
	// 				}

	// 				ChatPackets::SendSystemMessage(sysAddr, u"------");
	// 				ChatPackets::SendSystemMessage(sysAddr, u"Spawner ID: " + GeneralUtils::to_u16string(closest->GetSpawnerID()));
	// 			} else if (args[1] == "-p") {
	// 				const auto postion = closest->GetPosition();

	// 				ChatPackets::SendSystemMessage(
	// 					sysAddr,
	// 					GeneralUtils::ASCIIToUTF16("< " + std::to_string(postion.x) + ", " + std::to_string(postion.y) + ", " + std::to_string(postion.z) + " >")
	// 				);
	// 			} else if (args[1] == "-f") {
	// 				auto* destuctable = closest->GetComponent<DestroyableComponent>();

	// 				if (destuctable == nullptr) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"No destroyable component on this entity!");
	// 					return;
	// 				}

	// 				ChatPackets::SendSystemMessage(sysAddr, u"Smashable: " + (GeneralUtils::to_u16string(destuctable->GetIsSmashable())));

	// 				ChatPackets::SendSystemMessage(sysAddr, u"Friendly factions:");
	// 				for (const auto entry : destuctable->GetFactionIDs()) {
	// 					ChatPackets::SendSystemMessage(sysAddr, (GeneralUtils::to_u16string(entry)));
	// 				}

	// 				ChatPackets::SendSystemMessage(sysAddr, u"Enemy factions:");
	// 				for (const auto entry : destuctable->GetEnemyFactionsIDs()) {
	// 					ChatPackets::SendSystemMessage(sysAddr, (GeneralUtils::to_u16string(entry)));
	// 				}

	// 				if (args.size() >= 3) {
	// 					const auto faction = GeneralUtils::TryParse<int32_t>(args[2]);
	// 					if (!faction) return;

	// 					destuctable->SetFaction(-1);
	// 					destuctable->AddFaction(faction.value(), true);
	// 				}
	// 			} else if (args[1] == "-cf") {
	// 				auto* destuctable = entity->GetComponent<DestroyableComponent>();
	// 				if (!destuctable) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"No destroyable component on this entity!");
	// 					return;
	// 				}
	// 				if (destuctable->IsEnemy(closest)) ChatPackets::SendSystemMessage(sysAddr, u"They are our enemy");
	// 				else ChatPackets::SendSystemMessage(sysAddr, u"They are NOT our enemy");
	// 			} else if (args[1] == "-t") {
	// 				auto* phantomPhysicsComponent = closest->GetComponent<PhantomPhysicsComponent>();

	// 				if (phantomPhysicsComponent != nullptr) {
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Type: " + (GeneralUtils::to_u16string(static_cast<uint32_t>(phantomPhysicsComponent->GetEffectType()))));
	// 					const auto dir = phantomPhysicsComponent->GetDirection();
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Direction: <" + (GeneralUtils::to_u16string(dir.x)) + u", " + (GeneralUtils::to_u16string(dir.y)) + u", " + (GeneralUtils::to_u16string(dir.z)) + u">");
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Multiplier: " + (GeneralUtils::to_u16string(phantomPhysicsComponent->GetDirectionalMultiplier())));
	// 					ChatPackets::SendSystemMessage(sysAddr, u"Active: " + (GeneralUtils::to_u16string(phantomPhysicsComponent->GetPhysicsEffectActive())));
	// 				}

	// 				auto* triggerComponent = closest->GetComponent<TriggerComponent>();
	// 				if (triggerComponent) {
	// 					auto trigger = triggerComponent->GetTrigger();
	// 					if (trigger) {
	// 						ChatPackets::SendSystemMessage(sysAddr, u"Trigger: " + (GeneralUtils::to_u16string(trigger->id)));
	// 					}
	// 				}
	// 			}
	// 		}
	// 	}
}

void SlashCommandHandler::SendAnnouncement(const std::string& title, const std::string& message) {
	AMFArrayValue args;

	args.Insert("title", title);
	args.Insert("message", message);

	GameMessages::SendUIMessageServerToAllClients("ToggleAnnounce", args);

	//Notify chat about it
	CBITSTREAM;
	BitStreamUtils::WriteHeader(bitStream, eConnectionType::CHAT_INTERNAL, eChatInternalMessageType::ANNOUNCEMENT);

	bitStream.Write<uint32_t>(title.size());
	for (auto character : title) {
		bitStream.Write<char>(character);
	}

	bitStream.Write<uint32_t>(message.size());
	for (auto character : message) {
		bitStream.Write<char>(character);
	}

	Game::chatServer->Send(&bitStream, SYSTEM_PRIORITY, RELIABLE, 0, Game::chatSysAddr, false);
}
