#include "ProximityMonitorComponent.h"
#include "GameMessages.h"
#include "dZoneManager.h"
#include "ControllablePhysicsComponent.h"
#include "EntityManager.h"
#include "SimplePhysicsComponent.h"

const std::map<LWOOBJID, dpEntity*> ProximityMonitorComponent::m_EmptyObjectMap = {};

ProximityMonitorComponent::ProximityMonitorComponent(Entity* parent, int radiusSmall, int radiusLarge) : Component(parent) {
	if (radiusSmall != -1 && radiusLarge != -1) {
		SetProximityRadius(radiusSmall, "rocketSmall");
		SetProximityRadius(radiusLarge, "rocketLarge");
	}
}

ProximityMonitorComponent::~ProximityMonitorComponent() {
	for (const auto& en : m_ProximitiesData) {
		if (!en.second) continue;

		dpWorld::Instance().RemoveEntity(en.second);
	}

	m_ProximitiesData.clear();
}

void ProximityMonitorComponent::SetProximityRadius(float proxRadius, const std::string& name) {
	dpEntity* en = new dpEntity(m_OwningEntity->GetObjectID(), proxRadius);
	en->SetPosition(m_OwningEntity->GetPosition());

	dpWorld::Instance().AddEntity(en);
	m_ProximitiesData.insert(std::make_pair(name, en));
}

void ProximityMonitorComponent::SetProximityRadius(dpEntity* entity, const std::string& name) {
	dpWorld::Instance().AddEntity(entity);
	entity->SetPosition(m_OwningEntity->GetPosition());
	m_ProximitiesData.insert(std::make_pair(name, entity));
}

const std::map<LWOOBJID, dpEntity*>& ProximityMonitorComponent::GetProximityObjects(const std::string& name) {
	const auto& iter = m_ProximitiesData.find(name);

	if (iter == m_ProximitiesData.end()) {
		return m_EmptyObjectMap;
	}

	return iter->second->GetCurrentlyCollidingObjects();
}

bool ProximityMonitorComponent::IsInProximity(const std::string& name, LWOOBJID objectID) {
	const auto& iter = m_ProximitiesData.find(name);

	if (iter == m_ProximitiesData.end()) {
		return false;
	}

	const auto& collitions = iter->second->GetCurrentlyCollidingObjects();

	return collitions.find(objectID) != collitions.end();
}

void ProximityMonitorComponent::Update(float deltaTime) {
	for (const auto& prox : m_ProximitiesData) {
		if (!prox.second) continue;

		//Process enter events
		for (auto* en : prox.second->GetNewObjects()) {
			m_OwningEntity->OnCollisionProximity(en->GetObjectID(), prox.first, "ENTER");
		}

		//Process exit events
		for (auto* en : prox.second->GetRemovedObjects()) {
			m_OwningEntity->OnCollisionProximity(en->GetObjectID(), prox.first, "LEAVE");
		}
	}
}

