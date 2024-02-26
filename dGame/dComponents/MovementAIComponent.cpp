#include "MovementAIComponent.h"

#include <utility>
#include <cmath>

#include "ControllablePhysicsComponent.h"
#include "BaseCombatAIComponent.h"
#include "dpCommon.h"
#include "dpWorld.h"
#include "EntityManager.h"
#include "SimplePhysicsComponent.h"
#include "CDClientManager.h"
#include "Game.h"
#include "dZoneManager.h"
#include "eTriggerEventType.h"
#include "eWaypointCommandType.h"
#include "RenderComponent.h"
#include "SkillComponent.h"
#include "InventoryComponent.h"
#include "ProximityMonitorComponent.h"
#include "DestroyableComponent.h"

#include "CDComponentsRegistryTable.h"
#include "CDPhysicsComponentTable.h"

#include "dNavMesh.h"

namespace {
	/**
	 * Cache of all lots and their respective speeds
	 */
	std::map<LOT, float> m_PhysicsSpeedCache;
}

MovementAIComponent::MovementAIComponent(Entity* parent, MovementAIInfo info) : Component(parent) {
	m_Info = info;
	m_IsPaused = true;
	m_AtFinalWaypoint = true;

	m_BaseCombatAI = nullptr;

	m_BaseCombatAI = m_Parent->GetComponent<BaseCombatAIComponent>();

	//Try and fix the insane values:
	if (m_Info.wanderRadius > 5.0f) m_Info.wanderRadius *= 0.5f;
	if (m_Info.wanderRadius > 8.0f) m_Info.wanderRadius = 8.0f;
	if (m_Info.wanderSpeed > 0.5f) m_Info.wanderSpeed *= 0.5f;

	m_BaseSpeed = GetBaseSpeed(m_Parent->GetLOT());

	m_NextWaypoint = m_Parent->GetPosition();
	m_Acceleration = 0.4f;
	m_PullingToPoint = false;
	m_PullPoint = NiPoint3Constant::ZERO;
	m_HaltDistance = 0;
	m_TimeToTravel = 0;
	m_TimeTravelled = 0;
	m_CurrentSpeed = 0;
	m_MaxSpeed = 0;
	m_StartingWaypointIndex = -1;
	m_CurrentPathWaypointIndex = 0;
	m_LockRotation = false;
	m_IsInReverse = false;
	m_NextPathWaypointIndex = 0;
}

float MovementAIComponent::GetCurrentPathWaypointSpeed() const {
	if (!m_Path || m_CurrentPathWaypointIndex >= m_CurrentPath.size() || m_CurrentPathWaypointIndex < 0) {
		return 1.0f;
	}
	return m_Path->pathWaypoints.at(m_CurrentPathWaypointIndex).movingPlatform.speed;
}

void MovementAIComponent::SetupPath(const std::string& pathname) {
	std::string path = pathname;
	if (path.empty()) {
		path = m_Parent->GetVarAsString(u"attached_path");
		if (path.empty()) {
			LOG("No path to load for %i:%llu", m_Parent->GetLOT(), m_Parent->GetObjectID());
			return;
		}
	}
	const Path* pathData = Game::zoneManager->GetZone()->GetPath(path);
	if (pathData) {
		LOG("found path %i %s", m_Parent->GetLOT(), path.c_str());
		m_Path = pathData;
		if (!HasAttachedPathStart() && m_Parent->HasVar(u"attached_path_start")) m_StartingWaypointIndex = m_Parent->GetVar<uint32_t>(u"attached_path_start");
		if (m_Path && HasAttachedPathStart() && (m_StartingWaypointIndex < 0 || m_StartingWaypointIndex >= m_Path->pathWaypoints.size())) {
			LOG("WARNING: attached path start is out of bounds for %i:%llu, defaulting path start to 0",
				m_Parent->GetLOT(), m_Parent->GetObjectID());
			m_StartingWaypointIndex = 0;
		}
		std::vector<NiPoint3> waypoints;
		for (const auto& waypoint : m_Path->pathWaypoints) {
			waypoints.push_back(waypoint.position);
		}
		SetPath(waypoints);
	} else {
		LOG("No path found for %i:%llu", m_Parent->GetLOT(), m_Parent->GetObjectID());
	}
}

void MovementAIComponent::Update(const float deltaTime) {
	if (m_PullingToPoint) {
		const auto source = GetCurrentWaypoint();

		// Just a guess at the speed...
		const auto speed = deltaTime * 2.5f;

		NiPoint3 velocity = (m_PullPoint - source) * speed;

		SetPosition(source + velocity);

		// We are close enough to the pulled to point, stop pulling
		if (Vector3::DistanceSquared(m_Parent->GetPosition(), m_PullPoint) < std::pow(2, 2)) {
			m_PullingToPoint = false;
		}

		return;
	}

	// Are we done or paused?
	if (AtFinalWaypoint() || IsPaused()) return;

	if (m_HaltDistance > 0) {
		// Prevent us from hugging the target
		if (Vector3::DistanceSquared(ApproximateLocation(), GetDestination()) < std::pow(m_HaltDistance, 2)) {
			Stop();
			return;
		}
	}

	m_TimeTravelled += deltaTime;
	if (m_TimeTravelled < m_TimeToTravel) return;
	m_TimeTravelled = 0.0f;

	const auto source = GetCurrentWaypoint();

	SetPosition(source);

	NiPoint3 velocity = NiPoint3Constant::ZERO;

	// If we have no acceleration, then we have no max speed.
	// If we have no base speed, then we cannot scale the speed by it.
	// Do we have another waypoint to seek?
	if (m_Acceleration > 0 && m_BaseSpeed > 0 && AdvanceWaypointIndex()) {
		m_NextWaypoint = GetCurrentWaypoint();

		if (m_NextWaypoint == source) {
			m_TimeToTravel = 0.0f;

			return;
		}

		if (m_CurrentSpeed < m_MaxSpeed) {
			m_CurrentSpeed += m_Acceleration;
		}

		if (m_CurrentSpeed > m_MaxSpeed) {
			m_CurrentSpeed = m_MaxSpeed;
		}

		const auto speed = m_CurrentSpeed * m_BaseSpeed; // scale speed based on base speed * current speed

		const auto delta = m_NextWaypoint - source;

		// Normalize the vector
		const auto length = delta.Length();
		if (length > 0) {
			velocity = (delta / length).Unitize() * speed;
		}

		// Calclute the time it will take to reach the next waypoint with the current speed
		m_TimeTravelled = 0.0f;
		m_TimeToTravel = length / speed;

		SetRotation(NiQuaternion::LookAt(source, m_NextWaypoint));
	} else {
		// Check if there are more waypoints in the queue, if so set our next destination to the next waypoint
		// All checks for how to progress when you arrive at a waypoint will be handled in this else block.
		HandleWaypointArrived(0);
		return;
	}

nextAction:

	SetVelocity(velocity);

	Game::entityManager->SerializeEntity(m_Parent);
}

void MovementAIComponent::ReversePath() {
	if (m_CurrentPath.empty()) return;
	if (m_NextPathWaypointIndex < 0) m_NextPathWaypointIndex = 0;
	if (m_NextPathWaypointIndex >= m_CurrentPath.size()) m_NextPathWaypointIndex = m_CurrentPath.size() - 1;
	m_CurrentPathWaypointIndex = m_NextPathWaypointIndex;
	m_IsInReverse = !m_IsInReverse;
	AdvancePathWaypointIndex();
}

bool MovementAIComponent::AdvancePathWaypointIndex() {
	if (m_CurrentPath.empty()) return false;
	m_CurrentPathWaypointIndex = m_NextPathWaypointIndex;
	if (m_IsInReverse) {
		if (m_CurrentPathWaypointIndex >= 0) m_NextPathWaypointIndex--;
		return m_CurrentPathWaypointIndex >= 0;
	} else {
		if (m_CurrentPathWaypointIndex <= m_CurrentPath.size()) m_NextPathWaypointIndex++;
		return m_CurrentPathWaypointIndex < m_CurrentPath.size();
	}
}

const MovementAIInfo& MovementAIComponent::GetInfo() const {
	return m_Info;
}

bool MovementAIComponent::AdvanceWaypointIndex() {
	if (m_PathIndex >= m_InterpolatedWaypoints.size()) {
		return false;
	}

	m_PathIndex++;

	return true;
}

NiPoint3 MovementAIComponent::GetCurrentWaypoint() const {
	return m_PathIndex >= m_InterpolatedWaypoints.size() ? m_Parent->GetPosition() : m_InterpolatedWaypoints[m_PathIndex];
}

NiPoint3 MovementAIComponent::ApproximateLocation() const {
	auto source = m_Parent->GetPosition();
	if (AtFinalWaypoint()) return source;
	NiPoint3 approximation = source;

	// Only have physics sim for controllable physics
	if (!m_Parent->HasComponent(ControllablePhysicsComponent::ComponentType)) {
		auto destination = GetNextWaypoint();
		auto percentageToWaypoint = m_TimeToTravel > 0 ? m_TimeTravelled / m_TimeToTravel : 0;
		approximation = source + ((destination - source) * percentageToWaypoint);
	}

	if (dpWorld::IsLoaded()) {
		approximation.y = dpWorld::GetNavMesh()->GetHeightAtPoint(approximation);
	}

	return approximation;
}

bool MovementAIComponent::Warp(const NiPoint3& point) {
	Stop();

	NiPoint3 destination = point;

	if (dpWorld::IsLoaded()) {
		destination.y = dpWorld::GetNavMesh()->GetHeightAtPoint(point);

		if (std::abs(destination.y - point.y) > 3) {
			return false;
		}
	}

	SetPosition(destination);

	Game::entityManager->SerializeEntity(m_Parent);

	return true;
}

void MovementAIComponent::Pause() {
	if (AtFinalWaypoint() || IsPaused()) return;
	SetPosition(ApproximateLocation());
	SetVelocity(NiPoint3Constant::ZERO);

	// Clear this as we may be somewhere else when we resume movement.
	m_InterpolatedWaypoints.clear();
	m_IsPaused = true;
	m_PathIndex = 0;
	m_TimeToTravel = 0;
	m_TimeTravelled = 0;
}

void MovementAIComponent::Resume() {
	if (AtFinalWaypoint() || !IsPaused()) return;
	m_IsPaused = false;
	SetDestination(GetCurrentPathWaypoint());
	SetMaxSpeed(GetCurrentPathWaypointSpeed());
}

void MovementAIComponent::Stop() {
	if (AtFinalWaypoint()) return;

	SetPosition(ApproximateLocation());

	SetVelocity(NiPoint3Constant::ZERO);

	m_TimeToTravel = 0;
	m_TimeTravelled = 0;

	m_AtFinalWaypoint = true;
	m_IsPaused = true;

	m_InterpolatedWaypoints.clear();
	m_CurrentPath.clear();

	m_PathIndex = 0;

	m_CurrentSpeed = 0;
	m_CurrentPathWaypointIndex = 0;

	Game::entityManager->SerializeEntity(m_Parent);
}

void MovementAIComponent::PullToPoint(const NiPoint3& point) {
	Stop();

	m_PullingToPoint = true;
	m_PullPoint = point;
}

const NiPoint3& MovementAIComponent::GetCurrentPathWaypoint() const {
	if (m_CurrentPathWaypointIndex >= m_CurrentPath.size() || m_CurrentPathWaypointIndex < 0) {
		return m_Parent->GetPosition();
	}
	return m_CurrentPath.at(m_CurrentPathWaypointIndex);
}

void MovementAIComponent::SetPath(const std::vector<NiPoint3>& path, bool startInReverse) {
	if (path.empty()) return;
	m_CurrentPath = path;
	m_IsInReverse = startInReverse;

	// Start the Entity out at the first waypoint with their next waypoint being the same one.
	// This is so AdvancePathWaypointIndex can do the recovery from effectively a paused state.
	m_CurrentPathWaypointIndex = m_IsInReverse ? m_CurrentPath.size() - 1 : 0;
	m_NextPathWaypointIndex = m_IsInReverse ? m_CurrentPath.size() - 1 : 0;

	if (HasAttachedPathStart()) {
		m_CurrentPathWaypointIndex = m_StartingWaypointIndex;
		m_NextPathWaypointIndex = m_StartingWaypointIndex;
	}

	AdvancePathWaypointIndex();
	SetDestination(GetCurrentPathWaypoint());
	SetMaxSpeed(GetCurrentPathWaypointSpeed());
}

float MovementAIComponent::GetBaseSpeed(LOT lot) {
	// Check if the lot is in the cache
	const auto& it = m_PhysicsSpeedCache.find(lot);

	if (it != m_PhysicsSpeedCache.end()) {
		return it->second;
	}

	CDComponentsRegistryTable* componentRegistryTable = CDClientManager::GetTable<CDComponentsRegistryTable>();
	CDPhysicsComponentTable* physicsComponentTable = CDClientManager::GetTable<CDPhysicsComponentTable>();

	int32_t componentID;
	CDPhysicsComponent* physicsComponent = nullptr;

	componentID = componentRegistryTable->GetByIDAndType(lot, eReplicaComponentType::CONTROLLABLE_PHYSICS, -1);

	if (componentID == -1) {
		componentID = componentRegistryTable->GetByIDAndType(lot, eReplicaComponentType::SIMPLE_PHYSICS, -1);
	}

	physicsComponent = physicsComponentTable->GetByID(componentID);

	// Client defaults speed to 10 and if the speed is also null in the table, it defaults to 10.
	float speed = physicsComponent != nullptr ? physicsComponent->speed : 10.0f;

	float delta = fabs(speed) - 1.0f;

	if (delta <= std::numeric_limits<float>::epsilon()) speed = 10.0f;

	m_PhysicsSpeedCache[lot] = speed;

	return speed;
}

void MovementAIComponent::SetPosition(const NiPoint3& value) {
	m_Parent->SetPosition(value);
}

void MovementAIComponent::SetRotation(const NiQuaternion& value) {
	if (!m_LockRotation) m_Parent->SetRotation(value);
}

void MovementAIComponent::SetVelocity(const NiPoint3& value) {
	auto* controllablePhysicsComponent = m_Parent->GetComponent<ControllablePhysicsComponent>();

	if (controllablePhysicsComponent != nullptr) {
		controllablePhysicsComponent->SetVelocity(value);

		return;
	}

	auto* simplePhysicsComponent = m_Parent->GetComponent<SimplePhysicsComponent>();

	if (simplePhysicsComponent != nullptr) {
		simplePhysicsComponent->SetVelocity(value);
	}
}

void MovementAIComponent::SetDestination(const NiPoint3& destination) {
	if (m_PullingToPoint) return;

	const auto location = ApproximateLocation();

	if (!AtFinalWaypoint()) {
		SetPosition(location);
	}

	std::vector<NiPoint3> computedPath;
	if (dpWorld::IsLoaded()) {
		computedPath = dpWorld::GetNavMesh()->GetPath(m_Parent->GetPosition(), destination, m_Info.wanderSpeed);
	} else {
		// If we do not have a navmesh, we do not want an AI to be going towards points that are far below or above the map.
		//
	}

	// Somehow failed
	if (computedPath.empty()) {
		// Than take 10 points between the current position and the destination and make that the path

		auto start = location;

		auto delta = destination - start;

		auto step = delta / 10.0f;

		for (int i = 0; i < 10; i++) {
			start += step;

			computedPath.push_back(start);
		}
	}

	m_InterpolatedWaypoints.clear();

	// Simply path
	for (auto& point : computedPath) {
		if (dpWorld::IsLoaded()) {
			point.y = dpWorld::GetNavMesh()->GetHeightAtPoint(point);
		}

		m_InterpolatedWaypoints.push_back(point);
	}

	m_PathIndex = 0;

	m_TimeTravelled = 0;
	m_TimeToTravel = 0;

	m_AtFinalWaypoint = false;
	m_IsPaused = false;
}

NiPoint3 MovementAIComponent::GetDestination() const {
	return m_InterpolatedWaypoints.empty() ? m_Parent->GetPosition() : m_InterpolatedWaypoints.back();
}

void MovementAIComponent::SetMaxSpeed(const float value) {
	if (value == m_MaxSpeed) return;
	m_MaxSpeed = value;
	m_Acceleration = value / 5;
}

void MovementAIComponent::HandleWaypointArrived(uint32_t commandIndex) {
	m_Parent->TriggerEvent(eTriggerEventType::ARRIVED);
	m_Parent->TriggerEvent(eTriggerEventType::ARRIVED_AT_DESIRED_WAYPOINT);
	if (!m_Path || commandIndex >= m_Path->pathWaypoints.at(m_CurrentPathWaypointIndex).commands.size()) {
		if (!AdvancePathWaypointIndex()) {
			// We only want to handle path logic if we actually have a path setup for following
			if (m_Path && !m_CurrentPath.empty()) {
				if (m_Path->pathBehavior == PathBehavior::Bounce) {
					ReversePath();
				} else if (m_Path->pathBehavior == PathBehavior::Loop) {
					m_CurrentPathWaypointIndex = 0;
					m_NextPathWaypointIndex = 0;
					AdvancePathWaypointIndex();
					SetDestination(GetCurrentPathWaypoint());
					SetMaxSpeed(GetCurrentPathWaypointSpeed());
				} else {
					Stop();
					m_Parent->TriggerEvent(eTriggerEventType::ARRIVED_AT_END_OF_PATH);
				}
			} else {
				Stop();
			}
			return;
		}
		SetDestination(GetCurrentPathWaypoint());
		SetMaxSpeed(GetCurrentPathWaypointSpeed());
		return;
	}
	if (!IsPaused()) Pause();
	const auto& data = m_Path->pathWaypoints.at(m_CurrentPathWaypointIndex).commands.at(commandIndex).data;
	const auto& command = m_Path->pathWaypoints.at(m_CurrentPathWaypointIndex).commands.at(commandIndex).command;
	float delay = 0.0f;
	switch (command) {
	case eWaypointCommandType::STOP:
		Stop();
		break;
	case eWaypointCommandType::GROUP_EMOTE:
		delay = HandleWaypointCommandGroupEmote(data);
		break;
	case eWaypointCommandType::SET_VARIABLE:
		HandleWaypointCommandSetVariable(data);
		break;
	case eWaypointCommandType::CAST_SKILL:
		HandleWaypointCommandCastSkill(data);
		break;
	case eWaypointCommandType::EQUIP_INVENTORY:
		HandleWaypointCommandEquipInventory(data);
		break;
	case eWaypointCommandType::UNEQUIP_INVENTORY:
		HandleWaypointCommandUnequipInventory(data);
		break;
	case eWaypointCommandType::DELAY:
		delay = HandleWaypointCommandDelay(data);
		break;
	case eWaypointCommandType::EMOTE:
		delay = RenderComponent::PlayAnimation(m_Parent, data);
		break;
	case eWaypointCommandType::TELEPORT:
		HandleWaypointCommandTeleport(data);
		break;
	case eWaypointCommandType::PATH_SPEED:
		HandleWaypointCommandPathSpeed(data);
		break;
	case eWaypointCommandType::REMOVE_NPC:
		HandleWaypointCommandRemoveNPC(data);
		break;
	case eWaypointCommandType::CHANGE_WAYPOINT:
		HandleWaypointCommandChangeWaypoint(data);
		break;
	case eWaypointCommandType::KILL_SELF:
		m_Parent->Smash(LWOOBJID_EMPTY, eKillType::SILENT);
		break;
	case eWaypointCommandType::DELETE_SELF:
		m_Parent->Kill();
		break;
	case eWaypointCommandType::SPAWN_OBJECT:
		HandleWaypointCommandSpawnObject(data);
		break;
	case eWaypointCommandType::PLAY_SOUND:
		GameMessages::SendPlayNDAudioEmitter(m_Parent, UNASSIGNED_SYSTEM_ADDRESS, data);
		break;
	case eWaypointCommandType::BOUNCE:
		LOG("Unable to process bounce waypoint command server side!");
		break;
	case eWaypointCommandType::INVALID:
	default:
		LOG("Got invalid waypoint command %i", command);
		break;
	}

	m_Parent->AddCallbackTimer(delay, [this, commandIndex]() {
		this->HandleWaypointArrived(commandIndex + 1);
		}
	);
}

float MovementAIComponent::HandleWaypointCommandGroupEmote(const std::string& data) {
	const auto& split = GeneralUtils::SplitString(data, ';');
	if (split.size() != 2) return 0.0f;
	const auto& entities = Game::entityManager->GetEntitiesInGroup(split.at(0));
	float delay = 0.0f;
	for (auto& entity : entities) {
		delay = RenderComponent::PlayAnimation(entity, split.at(1));
	}
	return delay;
}

void MovementAIComponent::HandleWaypointCommandSetVariable(const std::string& data) {
	const auto& split = GeneralUtils::SplitString(data, ',');
	m_Parent->SetNetworkVar(GeneralUtils::ASCIIToUTF16(split.at(0)), split.at(1));
}

void MovementAIComponent::HandleWaypointCommandCastSkill(const std::string& data) {
	if (data.empty()) return;
	auto* skillComponent = m_Parent->GetComponent<SkillComponent>();
	if (!skillComponent) {
		LOG("Skill component not found!");
		return;
	}
	auto skillId = GeneralUtils::TryParse<uint32_t>(data);
	if (skillId && skillId != 0) skillComponent->CastSkill(skillId.value());
}

void MovementAIComponent::HandleWaypointCommandEquipInventory(const std::string& data) {
	if (data.empty()) return;
	auto* inventoryComponent = m_Parent->GetComponent<InventoryComponent>();
	if (!inventoryComponent) {
		LOG("Inventory component not found!");
		return;
	}
	// the client says use slot 0 of items
	const auto inventory = inventoryComponent->GetInventory(eInventoryType::ITEMS);
	if (!inventory) return;
	const auto slots = inventory->GetSlots();
	const auto item = slots.find(0);
	if (item != slots.end()) inventoryComponent->EquipItem(item->second);
}

void MovementAIComponent::HandleWaypointCommandUnequipInventory(const std::string& data) {
	if (data.empty()) return;
	auto* inventoryComponent = m_Parent->GetComponent<InventoryComponent>();
	if (!inventoryComponent) {
		LOG("Inventory component not found!");
		return;
	}
	// the client says use slot 0 of items
	const auto inventory = inventoryComponent->GetInventory(eInventoryType::ITEMS);
	if (!inventory) return;
	const auto slots = inventory->GetSlots();
	const auto item = slots.find(0);
	if (item != slots.end()) inventoryComponent->UnEquipItem(item->second);
}

float MovementAIComponent::HandleWaypointCommandDelay(const std::string& data) {
	auto delay = GeneralUtils::TryParse<float>(data);
	if (!delay) {
		LOG("Failed to parse delay %s", data.c_str());
	}
	return delay.value_or(0.0f);
}

void MovementAIComponent::HandleWaypointCommandTeleport(const std::string& data) {
	auto posString = GeneralUtils::SplitString(data, ',');
	if (posString.size() == 0) return;
	auto newPos = NiPoint3();
	std::optional<float> intermediate;
	if (posString.size() >= 1) {
		intermediate = GeneralUtils::TryParse<float>(posString.at(0));
		if (!intermediate) return;

		newPos.x = intermediate.value();
		if (posString.size() >= 2) {
			intermediate = GeneralUtils::TryParse<float>(posString.at(1));
			if (!intermediate) return;

			newPos.y = intermediate.value();
			if (posString.size() >= 3) {
				intermediate = GeneralUtils::TryParse<float>(posString.at(2));
				if (!intermediate) return;

				newPos.z = intermediate.value();
			}
		}
	}
	GameMessages::SendTeleport(m_Parent->GetObjectID(), newPos, NiQuaternionConstant::IDENTITY, UNASSIGNED_SYSTEM_ADDRESS);
}

void MovementAIComponent::HandleWaypointCommandPathSpeed(const std::string& data) {
	auto speed = GeneralUtils::TryParse<float>(data);
	if (!speed) return;
	SetMaxSpeed(speed.value());
}

void MovementAIComponent::HandleWaypointCommandRemoveNPC(const std::string& data) {
	if (data.empty()) return;
	auto* proximityMonitorComponent = m_Parent->GetComponent<ProximityMonitorComponent>();
	if (!proximityMonitorComponent) {
		LOG("Proximity monitor component not found!");
		return;
	}
	const auto foundObjs = proximityMonitorComponent->GetProximityObjects("KillOBJS");
	for (auto& [objid, phyEntity] : foundObjs) {
		auto entity = Game::entityManager->GetEntity(objid);
		if (!entity) return;
		auto* destroyableComponent = m_Parent->GetComponent<DestroyableComponent>();
		if (!destroyableComponent) {
			LOG("Destroyable component not found!");
			return;
		}
		int32_t factionID = -1;
		auto parsed = GeneralUtils::TryParse<uint32_t>(data);
		if (!parsed) return;
		factionID = parsed.value();
		if (destroyableComponent->BelongsToFaction(factionID)) m_Parent->Kill();
	}
}

void MovementAIComponent::HandleWaypointCommandChangeWaypoint(const std::string& data) {
	std::string path_string = "";
	int32_t index = 0;
	// sometimes there's a path and what waypoint to start, which are comma separated
	if (data.find(",") != std::string::npos) {
		auto datas = GeneralUtils::SplitString(data, ',');
		path_string = datas.at(0);
		auto parsed = GeneralUtils::TryParse<int32_t>(datas.at(1));
		if (!parsed) return;
		index = parsed.value();
	} else path_string = data;

	if (path_string != "") {
		SetPathStartingWaypointIndex(index);
		SetupPath(path_string);
	}
}

void MovementAIComponent::HandleWaypointCommandSpawnObject(const std::string& data) {
	LOT newObjectLOT = 0;
	auto parsed = GeneralUtils::TryParse<LOT>(data);
	if (!parsed) return;
	newObjectLOT = parsed.value();
	EntityInfo info{};
	info.lot = newObjectLOT;
	info.pos = m_Parent->GetPosition();
	info.rot = m_Parent->GetRotation();
	auto* spawnedEntity = Game::entityManager->CreateEntity(info, nullptr, m_Parent);
	Game::entityManager->ConstructEntity(spawnedEntity);
	m_Parent->Smash(LWOOBJID_EMPTY, eKillType::SILENT);
}

