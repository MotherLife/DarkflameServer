#include "Scene.h"

#include <tinyxml2.h>

#include "ServerPreconditions.hpp"
#include "EntityManager.h"
#include "EntityInfo.h"
#include "MissionComponent.h"

using namespace Cinema;

std::unordered_map<std::string, Scene> Scene::m_Scenes;

void Cinema::Scene::AddObject(LOT lot, NiPoint3 position, NiQuaternion rotation) {
	m_Objects.push_back(std::make_pair(lot, std::make_pair(position, rotation)));
}

void Scene::AddPrefab(const Prefab& prefab, NiPoint3 position) {
	m_Prefabs.push_back(std::make_pair(prefab, position));
}

void Cinema::Scene::AddNPC(LOT npc, const std::string& name, Recording::Recorder* act) {
	m_NPCs.push_back(std::make_pair(npc, std::make_pair(act, name)));
}

void Cinema::Scene::Rehearse() {
	CheckForShowings();
}

void Cinema::Scene::Conclude(Entity* player) const {
	if (player == nullptr) {
		return;
	}

	if (m_Audience.find(player->GetObjectID()) == m_Audience.end()) {
		return;
	}

	auto* missionComponent = player->GetComponent<MissionComponent>();

	if (missionComponent == nullptr) {
		return;
	}

	if (m_CompleteMission != 0) {
		missionComponent->CompleteMission(m_CompleteMission);
	}

	if (m_AcceptMission != 0) {
		missionComponent->AcceptMission(m_AcceptMission);
	}
}

bool Cinema::Scene::IsPlayerInBounds(Entity* player) const {
	if (player == nullptr) {
		return false;
	}

	if (m_Bounds == 0.0f) {
		return true;
	}

	const auto& position = player->GetPosition();
	
	auto distance = NiPoint3::Distance(position, m_Center);

	// The player may be within 20% of the bounds
	return distance <= (m_Bounds * 1.2f);
}

void Cinema::Scene::CheckForShowings() {
	auto audience = m_Audience;

	for (const auto& member : audience) {
		if (Game::entityManager->GetEntity(member) == nullptr) {
			m_Audience.erase(member);
		}
	}

	// I don't care
	Game::entityManager->GetZoneControlEntity()->AddCallbackTimer(1.0f, [this]() {
		for (auto* player : Player::GetAllPlayers()) {
			if (m_Audience.find(player->GetObjectID()) != m_Audience.end()) {
				continue;
			}

			CheckTicket(player);

			
		}
	});
}

void Cinema::Scene::CheckTicket(Entity* player) {
	if (m_Audience.find(player->GetObjectID()) != m_Audience.end()) {
		return;
	}

	for (const auto& [expression, invert] : m_Preconditions) {
		if (expression.Check(player) == invert) {
			return;
		}
	}

	m_Audience.emplace(player->GetObjectID());

	Act(player);
}

Play* Cinema::Scene::Act(Entity* player) {
	auto* play = new Play();

	if (player != nullptr) {
		play->player = player->GetObjectID();
	}

	play->scene = this;

	for (const auto& [lot, transform] : m_Objects) {
		const auto& [position, rotation] = transform;
		EntityInfo info;
		info.pos = position;
		info.rot = rotation;
		info.lot = lot;
		info.spawnerID = Game::entityManager->GetZoneControlEntity()->GetObjectID();

		auto* entity = Game::entityManager->CreateEntity(info);

		Game::entityManager->ConstructEntity(entity);

		if (player != nullptr) {
			ServerPreconditions::AddSoloActor(entity->GetObjectID(), player->GetObjectID());
		}

		Game::logger->Log("Scene", "Spawing object %d", entity->GetObjectID());
	}

	for (const auto& [prefab, position] : m_Prefabs) {
		const auto instanceId = prefab.Instantiate(position);

		const auto& entities = prefab.GetEntities(instanceId);

		if (player != nullptr) {
			for (const auto& entity : entities) {
				ServerPreconditions::AddSoloActor(entity, player->GetObjectID());

				play->entities.emplace(entity);
			}
		}

		Game::logger->Log("Scene", "Spawing prefab %d", instanceId);
	}

	for (const auto& [npc, meta] : m_NPCs) {
		const auto& [act, name] = meta;
		EntityInfo info;
		info.pos = NiPoint3();
		info.rot = NiQuaternion();
		info.lot = npc;
		info.spawnerID = Game::entityManager->GetZoneControlEntity()->GetObjectID();
		info.settings = {
			new LDFData<std::vector<std::u16string>>(u"syncLDF", { u"custom_script_client" }),
			new LDFData<std::u16string>(u"custom_script_client", u"scripts\\ai\\SPEC\\MISSION_MINIGAME_CLIENT.lua")
		};

		auto* entity = Game::entityManager->CreateEntity(info);

		play->entities.emplace(entity->GetObjectID());

		Game::entityManager->ConstructEntity(entity);

		Entity* actor; 

		if (player != nullptr) {
			actor = act->ActFor(entity, player, play);
		}
		else
		{
			act->Act(entity, play);

			actor = entity;
		}

		if (actor != nullptr) {
			actor->SetVar(u"npcName", name);
		}

		play->entities.emplace(entity->GetObjectID());

		Game::logger->Log("Scene", "Spawing npc %d", entity->GetObjectID());
	}

	if (player != nullptr) {
		play->SetupCheckForAudience();
	}

	return play;
}

Scene& Cinema::Scene::LoadFromFile(std::string file) {
	if (m_Scenes.find(file) != m_Scenes.end()) {
		return m_Scenes[file];
	}

	Scene scene;

	tinyxml2::XMLDocument doc;
	doc.LoadFile(file.c_str());

	tinyxml2::XMLElement* root = doc.FirstChildElement("Scene");
	if (!root) {
		Game::logger->Log("Scene", "Failed to load scene from file: %s", file.c_str());

		m_Scenes.emplace(file, scene);

		return m_Scenes[file];
	}

	// Load center and bounds
	if (root->Attribute("x")) {
		scene.m_Center = NiPoint3(root->FloatAttribute("x"), root->FloatAttribute("y"), root->FloatAttribute("z"));
	}

	if (root->Attribute("bounds")) {
		scene.m_Bounds = root->FloatAttribute("bounds");
	}

	// Load accept and complete mission
	if (root->Attribute("acceptMission")) {
		scene.m_AcceptMission = root->IntAttribute("acceptMission");
	}

	if (root->Attribute("completeMission")) {
		scene.m_CompleteMission = root->IntAttribute("completeMission");
	}

	// Load preconditions
	for (tinyxml2::XMLElement* element = root->FirstChildElement("Precondition"); element; element = element->NextSiblingElement("Precondition")) {
		scene.m_Preconditions.push_back(std::make_pair(Preconditions::CreateExpression(element->Attribute("expression")), element->BoolAttribute("not")));
	}

	for (tinyxml2::XMLElement* element = root->FirstChildElement("Object"); element; element = element->NextSiblingElement("Object")) {
		const auto lot = element->UnsignedAttribute("lot");
		const auto position = NiPoint3(element->FloatAttribute("x"), element->FloatAttribute("y"), element->FloatAttribute("z"));
		const auto rotation = NiQuaternion(element->FloatAttribute("qx"), element->FloatAttribute("qy"), element->FloatAttribute("qz"), element->FloatAttribute("qw"));

		scene.AddObject(lot, position, rotation);
	}

	for (tinyxml2::XMLElement* element = root->FirstChildElement("Prefab"); element; element = element->NextSiblingElement("Prefab")) {
		const auto prefab = element->Attribute("file");
		const auto position = NiPoint3(element->FloatAttribute("x"), element->FloatAttribute("y"), element->FloatAttribute("z"));

		scene.AddPrefab(Prefab::LoadFromFile(prefab), position);
	}

	for (tinyxml2::XMLElement* element = root->FirstChildElement("NPC"); element; element = element->NextSiblingElement("NPC")) {
		LOT npc = 2097253;
		if (element->Attribute("lot")) {
			npc = element->UnsignedAttribute("lot");
		}

		std::string name = "";

		if (element->Attribute("name")) {
			name = element->Attribute("name");
		}
		
		const auto act = element->Attribute("act");

		scene.AddNPC(npc, name, Recording::Recorder::LoadFromFile(act));
	}

	Game::logger->Log("Scene", "Loaded scene from file: %s", file.c_str());

	m_Scenes.emplace(file, scene);

	return m_Scenes[file];
}
