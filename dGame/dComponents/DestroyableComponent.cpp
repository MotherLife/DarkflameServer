#include "DestroyableComponent.h"
#include <BitStream.h>
#include "dLogger.h"
#include "Game.h"
#include "dConfig.h"

#include "Amf3.h"
#include "AmfSerialize.h"
#include "GameMessages.h"
#include "User.h"
#include "CDClientManager.h"
#include "CDDestructibleComponentTable.h"
#include "EntityManager.h"
#include "QuickBuildComponent.h"
#include "CppScripts.h"
#include "Loot.h"
#include "Character.h"
#include "Spawner.h"
#include "BaseCombatAIComponent.h"
#include "TeamManager.h"
#include "BuffComponent.h"
#include "SkillComponent.h"
#include "Item.h"

#include <sstream>
#include <algorithm>

#include "MissionComponent.h"
#include "CharacterComponent.h"
#include "PossessableComponent.h"
#include "PossessorComponent.h"
#include "InventoryComponent.h"
#include "dZoneManager.h"
#include "WorldConfig.h"
#include "eMissionTaskType.h"
#include "eStateChangeType.h"
#include "eGameActivity.h"

#include "CDComponentsRegistryTable.h"
#include "CDCurrencyTableTable.h"

DestroyableComponent::DestroyableComponent(Entity* parent, int32_t componentId) : Component(parent) {
	m_iArmor = 0;
	m_fMaxArmor = 0.0f;
	m_iImagination = 0;
	m_fMaxImagination = 0.0f;
	m_FactionIDs = std::vector<int32_t>();
	m_EnemyFactionIDs = std::vector<int32_t>();
	m_IsSmashable = false;
	m_IsDead = false;
	m_IsSmashed = false;
	m_IsGMImmune = false;
	m_IsShielded = false;
	m_DamageToAbsorb = 0;
	m_IsModuleAssembly = m_ParentEntity->HasComponent(eReplicaComponentType::MODULE_ASSEMBLY);
	m_DirtyThreatList = false;
	m_HasThreats = false;
	m_ExplodeFactor = 1.0f;
	m_iHealth = 0;
	m_fMaxHealth = 0;
	m_AttacksToBlock = 0;
	m_LootMatrixID = 0;
	m_MinCoins = 0;
	m_MaxCoins = 0;
	m_DamageReduction = 0;
	m_ComponentId = componentId;

	m_ImmuneToBasicAttackCount = 0;
	m_ImmuneToDamageOverTimeCount = 0;
	m_ImmuneToKnockbackCount = 0;
	m_ImmuneToInterruptCount = 0;
	m_ImmuneToSpeedCount = 0;
	m_ImmuneToImaginationGainCount = 0;
	m_ImmuneToImaginationLossCount = 0;
	m_ImmuneToQuickbuildInterruptCount = 0;
	m_ImmuneToPullToPointCount = 0;
}
void DestroyableComponent::Startup() {

}
void DestroyableComponent::LoadConfigData() {
	SetIsSmashable(GetIsSmashable() | (m_ParentEntity->GetVarAs<int32_t>(u"is_smashable") != 0));
}
void DestroyableComponent::LoadTemplateData() {
	if (m_ParentEntity->IsPlayer()) return;
	auto* destroyableComponentTable = CDClientManager::Instance().GetTable<CDDestructibleComponentTable>();
	auto destroyableDataLookup = destroyableComponentTable->Query([this](CDDestructibleComponent entry) { return (entry.id == this->m_ComponentId); });
	if (m_ComponentId == -1 || destroyableDataLookup.empty()) {
		SetHealth(1);
		SetArmor(0);

		SetMaxHealth(1);
		SetMaxArmor(0);

		SetIsSmashable(true);
		AddFaction(-1);
		AddFaction(6); //Smashables

		// A race car has 60 imagination, other entities defaults to 0.
		SetImagination(m_ParentEntity->HasComponent(eReplicaComponentType::RACING_STATS) ? 60 : 0);
		SetMaxImagination(m_ParentEntity->HasComponent(eReplicaComponentType::RACING_STATS) ? 60 : 0);
		return;
	}

	auto destroyableData = destroyableDataLookup.at(0);
	if (m_ParentEntity->HasComponent(eReplicaComponentType::RACING_STATS)) {
		destroyableData.imagination = 60;
	}

	SetHealth(destroyableData.life);
	SetImagination(destroyableData.imagination);
	SetArmor(destroyableData.armor);

	SetMaxHealth(destroyableData.life);
	SetMaxImagination(destroyableData.imagination);
	SetMaxArmor(destroyableData.armor);

	SetIsSmashable(destroyableData.isSmashable);

	SetLootMatrixID(destroyableData.LootMatrixIndex);

	// Now get currency information
	uint32_t npcMinLevel = destroyableData.level;
	uint32_t currencyIndex = destroyableData.CurrencyIndex;

	auto* currencyTable = CDClientManager::Instance().GetTable<CDCurrencyTableTable>();
	auto currencyValues = currencyTable->Query([=](CDCurrencyTable entry) { return (entry.currencyIndex == currencyIndex && entry.npcminlevel == npcMinLevel); });

	if (currencyValues.size() > 0) {
		// Set the coins
		SetMinCoins(currencyValues.at(0).minvalue);
		SetMaxCoins(currencyValues.at(0).maxvalue);
	}
	AddFaction(destroyableData.faction);
	std::stringstream ss(destroyableData.factionList);
	std::string tokenStr;

	while (std::getline(ss, tokenStr, ',')) {
		int32_t factionToAdd = -2;
		if (!GeneralUtils::TryParse(tokenStr, factionToAdd) || factionToAdd == -2 || factionToAdd == destroyableData.faction) continue;

		AddFaction(factionToAdd);
	}
}

void DestroyableComponent::Serialize(RakNet::BitStream* outBitStream, bool bIsInitialUpdate, uint32_t& flags) {
	if (bIsInitialUpdate) {
		outBitStream->Write1(); // always write these on construction
		outBitStream->Write(m_ImmuneToBasicAttackCount);
		outBitStream->Write(m_ImmuneToDamageOverTimeCount);
		outBitStream->Write(m_ImmuneToKnockbackCount);
		outBitStream->Write(m_ImmuneToInterruptCount);
		outBitStream->Write(m_ImmuneToSpeedCount);
		outBitStream->Write(m_ImmuneToImaginationGainCount);
		outBitStream->Write(m_ImmuneToImaginationLossCount);
		outBitStream->Write(m_ImmuneToQuickbuildInterruptCount);
		outBitStream->Write(m_ImmuneToPullToPointCount);
	}

	outBitStream->Write(m_DirtyHealth || bIsInitialUpdate);
	if (m_DirtyHealth || bIsInitialUpdate) {
		outBitStream->Write(m_iHealth);
		outBitStream->Write(m_fMaxHealth);
		outBitStream->Write(m_iArmor);
		outBitStream->Write(m_fMaxArmor);
		outBitStream->Write(m_iImagination);
		outBitStream->Write(m_fMaxImagination);

		outBitStream->Write(m_DamageToAbsorb);
		outBitStream->Write(IsImmune());
		outBitStream->Write(m_IsGMImmune);
		outBitStream->Write(m_IsShielded);

		outBitStream->Write(m_fMaxHealth);
		outBitStream->Write(m_fMaxArmor);
		outBitStream->Write(m_fMaxImagination);

		outBitStream->Write(uint32_t(m_FactionIDs.size()));
		for (size_t i = 0; i < m_FactionIDs.size(); ++i) {
			outBitStream->Write(m_FactionIDs[i]);
		}

		outBitStream->Write(m_IsSmashable);

		if (bIsInitialUpdate) {
			outBitStream->Write(m_IsDead);
			outBitStream->Write(m_IsSmashed);

			if (m_IsSmashable) {
				outBitStream->Write(m_IsModuleAssembly);
				outBitStream->Write(m_ExplodeFactor != 1.0f);
				if (m_ExplodeFactor != 1.0f) outBitStream->Write(m_ExplodeFactor);
			}
		}
		m_DirtyHealth = false;
	}

	outBitStream->Write(m_DirtyThreatList || bIsInitialUpdate);
	if (m_DirtyThreatList || bIsInitialUpdate) {
		outBitStream->Write(m_HasThreats);
		m_DirtyThreatList = false;
	}
}

void DestroyableComponent::LoadFromXml(tinyxml2::XMLDocument* doc) {
	tinyxml2::XMLElement* dest = doc->FirstChildElement("obj")->FirstChildElement("dest");
	if (!dest) {
		Game::logger->Log("DestroyableComponent", "Failed to find dest tag!");
		return;
	}

	auto* buffComponent = m_ParentEntity->GetComponent<BuffComponent>();

	if (buffComponent != nullptr) {
		buffComponent->LoadFromXml(doc);
	}

	dest->QueryAttribute("hc", &m_iHealth);
	dest->QueryAttribute("hm", &m_fMaxHealth);
	dest->QueryAttribute("im", &m_fMaxImagination);
	dest->QueryAttribute("ic", &m_iImagination);
	dest->QueryAttribute("ac", &m_iArmor);
	dest->QueryAttribute("am", &m_fMaxArmor);
	m_DirtyHealth = true;
}

void DestroyableComponent::UpdateXml(tinyxml2::XMLDocument* doc) {
	tinyxml2::XMLElement* dest = doc->FirstChildElement("obj")->FirstChildElement("dest");
	if (!dest) {
		Game::logger->Log("DestroyableComponent", "Failed to find dest tag!");
		return;
	}

	auto* buffComponent = m_ParentEntity->GetComponent<BuffComponent>();

	if (buffComponent != nullptr) {
		buffComponent->UpdateXml(doc);
	}

	dest->SetAttribute("hc", m_iHealth);
	dest->SetAttribute("hm", m_fMaxHealth);
	dest->SetAttribute("im", m_fMaxImagination);
	dest->SetAttribute("ic", m_iImagination);
	dest->SetAttribute("ac", m_iArmor);
	dest->SetAttribute("am", m_fMaxArmor);
}

void DestroyableComponent::SetHealth(int32_t value) {
	m_DirtyHealth = true;

	auto* characterComponent = m_ParentEntity->GetComponent<CharacterComponent>();
	if (characterComponent != nullptr) {
		characterComponent->TrackHealthDelta(value - m_iHealth);
	}

	m_iHealth = value;
}

void DestroyableComponent::SetMaxHealth(float value, bool playAnim) {
	m_DirtyHealth = true;
	// Used for playAnim if opted in for.
	int32_t difference = static_cast<int32_t>(std::abs(m_fMaxHealth - value));
	m_fMaxHealth = value;

	if (m_iHealth > m_fMaxHealth) {
		m_iHealth = m_fMaxHealth;
	}

	if (playAnim) {
		// Now update the player bar
		if (!m_ParentEntity->GetParentUser()) return;

		AMFArrayValue args;
		args.Insert("amount", std::to_string(difference));
		args.Insert("type", "health");

		GameMessages::SendUIMessageServerToSingleClient(m_ParentEntity, m_ParentEntity->GetParentUser()->GetSystemAddress(), "MaxPlayerBarUpdate", args);
	}

	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}

void DestroyableComponent::SetArmor(int32_t value) {
	m_DirtyHealth = true;

	// If Destroyable Component already has zero armor do not trigger the passive ability again.
	bool hadArmor = m_iArmor > 0;

	auto* characterComponent = m_ParentEntity->GetComponent<CharacterComponent>();
	if (characterComponent != nullptr) {
		characterComponent->TrackArmorDelta(value - m_iArmor);
	}

	m_iArmor = value;

	auto* inventroyComponent = m_ParentEntity->GetComponent<InventoryComponent>();
	if (m_iArmor == 0 && inventroyComponent != nullptr && hadArmor) {
		inventroyComponent->TriggerPassiveAbility(PassiveAbilityTrigger::SentinelArmor);
	}
}

void DestroyableComponent::SetMaxArmor(float value, bool playAnim) {
	m_DirtyHealth = true;
	m_fMaxArmor = value;

	if (m_iArmor > m_fMaxArmor) {
		m_iArmor = m_fMaxArmor;
	}

	if (playAnim) {
		// Now update the player bar
		if (!m_ParentEntity->GetParentUser()) return;

		AMFArrayValue args;
		args.Insert("amount", std::to_string(value));
		args.Insert("type", "armor");

		GameMessages::SendUIMessageServerToSingleClient(m_ParentEntity, m_ParentEntity->GetParentUser()->GetSystemAddress(), "MaxPlayerBarUpdate", args);
	}

	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}

void DestroyableComponent::SetImagination(int32_t value) {
	m_DirtyHealth = true;

	auto* characterComponent = m_ParentEntity->GetComponent<CharacterComponent>();
	if (characterComponent != nullptr) {
		characterComponent->TrackImaginationDelta(value - m_iImagination);
	}

	m_iImagination = value;

	auto* inventroyComponent = m_ParentEntity->GetComponent<InventoryComponent>();
	if (m_iImagination == 0 && inventroyComponent != nullptr) {
		inventroyComponent->TriggerPassiveAbility(PassiveAbilityTrigger::AssemblyImagination);
	}
}

void DestroyableComponent::SetMaxImagination(float value, bool playAnim) {
	m_DirtyHealth = true;
	// Used for playAnim if opted in for.
	int32_t difference = static_cast<int32_t>(std::abs(m_fMaxImagination - value));
	m_fMaxImagination = value;

	if (m_iImagination > m_fMaxImagination) {
		m_iImagination = m_fMaxImagination;
	}

	if (playAnim) {
		// Now update the player bar
		if (!m_ParentEntity->GetParentUser()) return;

		AMFArrayValue args;
		args.Insert("amount", std::to_string(difference));
		args.Insert("type", "imagination");

		GameMessages::SendUIMessageServerToSingleClient(m_ParentEntity, m_ParentEntity->GetParentUser()->GetSystemAddress(), "MaxPlayerBarUpdate", args);
	}
	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}

void DestroyableComponent::SetDamageToAbsorb(int32_t value) {
	m_DirtyHealth = true;
	m_DamageToAbsorb = value;
}

void DestroyableComponent::SetDamageReduction(int32_t value) {
	m_DirtyHealth = true;
	m_DamageReduction = value;
}

void DestroyableComponent::SetIsImmune(bool value) {
	m_DirtyHealth = true;
	m_ImmuneToBasicAttackCount = value ? 1 : 0;
}

void DestroyableComponent::SetIsGMImmune(bool value) {
	m_DirtyHealth = true;
	m_IsGMImmune = value;
}

void DestroyableComponent::SetIsShielded(bool value) {
	m_DirtyHealth = true;
	m_IsShielded = value;
}

void DestroyableComponent::AddFaction(const int32_t factionID, const bool ignoreChecks) {
	// Ignore factionID -1
	if (factionID == -1 && !ignoreChecks) {
		return;
	}

	m_FactionIDs.push_back(factionID);
	m_DirtyHealth = true;

	auto query = CDClientDatabase::CreatePreppedStmt(
		"SELECT enemyList FROM Factions WHERE faction = ?;");
	query.bind(1, (int)factionID);

	auto result = query.execQuery();

	if (result.eof()) return;

	if (result.fieldIsNull(0)) return;

	const auto* list_string = result.getStringField(0);

	std::stringstream ss(list_string);
	std::string token;

	while (std::getline(ss, token, ',')) {
		if (token.empty()) continue;

		auto id = std::stoi(token);

		auto exclude = std::find(m_FactionIDs.begin(), m_FactionIDs.end(), id) != m_FactionIDs.end();

		if (!exclude) {
			exclude = std::find(m_EnemyFactionIDs.begin(), m_EnemyFactionIDs.end(), id) != m_EnemyFactionIDs.end();
		}

		if (exclude) {
			continue;
		}

		AddEnemyFaction(id);
	}

	result.finalize();
}

bool DestroyableComponent::IsEnemy(const Entity* other) const {
	const auto* otherDestroyableComponent = other->GetComponent<DestroyableComponent>();
	if (otherDestroyableComponent != nullptr) {
		for (const auto enemyFaction : m_EnemyFactionIDs) {
			for (const auto otherFaction : otherDestroyableComponent->GetFactionIDs()) {
				if (enemyFaction == otherFaction)
					return true;
			}
		}
	}

	return false;
}

bool DestroyableComponent::IsFriend(const Entity* other) const {
	const auto* otherDestroyableComponent = other->GetComponent<DestroyableComponent>();
	if (otherDestroyableComponent != nullptr) {
		for (const auto enemyFaction : m_EnemyFactionIDs) {
			for (const auto otherFaction : otherDestroyableComponent->GetFactionIDs()) {
				if (enemyFaction == otherFaction)
					return false;
			}
		}

		return true;
	}

	return false;
}

void DestroyableComponent::AddEnemyFaction(int32_t factionID) {
	m_EnemyFactionIDs.push_back(factionID);
}


void DestroyableComponent::SetIsSmashable(bool value) {
	m_DirtyHealth = true;
	m_IsSmashable = value;
}

void DestroyableComponent::SetAttacksToBlock(const uint32_t value) {
	m_AttacksToBlock = value;
}

bool DestroyableComponent::IsImmune() const {
	return m_IsGMImmune || m_ImmuneToBasicAttackCount > 0;
}

bool DestroyableComponent::IsKnockbackImmune() const {
	auto* characterComponent = m_ParentEntity->GetComponent<CharacterComponent>();
	auto* inventoryComponent = m_ParentEntity->GetComponent<InventoryComponent>();

	if (characterComponent != nullptr && inventoryComponent != nullptr && characterComponent->GetCurrentActivity() == eGameActivity::QUICKBUILDING) {
		const auto hasPassive = inventoryComponent->HasAnyPassive({
			eItemSetPassiveAbilityID::EngineerRank2, eItemSetPassiveAbilityID::EngineerRank3,
			eItemSetPassiveAbilityID::SummonerRank2, eItemSetPassiveAbilityID::SummonerRank3,
			eItemSetPassiveAbilityID::InventorRank2, eItemSetPassiveAbilityID::InventorRank3,
			}, 5);

		if (hasPassive) {
			return true;
		}
	}

	return IsImmune() || m_IsShielded || m_AttacksToBlock > 0;
}

bool DestroyableComponent::HasFaction(int32_t factionID) const {
	return std::find(m_FactionIDs.begin(), m_FactionIDs.end(), factionID) != m_FactionIDs.end();
}

LWOOBJID DestroyableComponent::GetKillerID() const {
	return m_KillerID;
}

Entity* DestroyableComponent::GetKiller() const {
	return EntityManager::Instance()->GetEntity(m_KillerID);
}

bool DestroyableComponent::CheckValidity(const LWOOBJID target, const bool ignoreFactions, const bool targetEnemy, const bool targetFriend) const {
	auto* targetEntity = EntityManager::Instance()->GetEntity(target);

	if (targetEntity == nullptr) {
		Game::logger->Log("DestroyableComponent", "Invalid entity for checking validity (%llu)!", target);
		return false;
	}

	auto* targetDestroyable = targetEntity->GetComponent<DestroyableComponent>();

	if (targetDestroyable == nullptr) {
		return false;
	}

	auto* targetQuickbuild = targetEntity->GetComponent<QuickBuildComponent>();

	if (targetQuickbuild != nullptr) {
		const auto state = targetQuickbuild->GetState();

		if (state != eRebuildState::COMPLETED) {
			return false;
		}
	}

	if (ignoreFactions) {
		return true;
	}

	// Get if the target entity is an enemy and friend
	bool isEnemy = IsEnemy(targetEntity);
	bool isFriend = IsFriend(targetEntity);

	// Return true if the target type matches what we are targeting
	return (isEnemy && targetEnemy) || (isFriend && targetFriend);
}


void DestroyableComponent::Heal(const uint32_t health) {
	auto current = static_cast<uint32_t>(GetHealth());
	const auto max = static_cast<uint32_t>(GetMaxHealth());

	current += health;

	current = std::min(current, max);

	SetHealth(current);

	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}


void DestroyableComponent::Imagine(const int32_t deltaImagination) {
	auto current = static_cast<int32_t>(GetImagination());
	const auto max = static_cast<int32_t>(GetMaxImagination());

	current += deltaImagination;

	current = std::min(current, max);

	if (current < 0) {
		current = 0;
	}

	SetImagination(current);

	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}


void DestroyableComponent::Repair(const uint32_t armor) {
	auto current = static_cast<uint32_t>(GetArmor());
	const auto max = static_cast<uint32_t>(GetMaxArmor());

	current += armor;

	current = std::min(current, max);

	SetArmor(current);

	EntityManager::Instance()->SerializeEntity(m_ParentEntity);
}


void DestroyableComponent::Damage(uint32_t damage, const LWOOBJID source, uint32_t skillID, bool echo) {
	if (GetHealth() <= 0) {
		return;
	}

	if (IsImmune()) {
		return;
	}

	if (m_AttacksToBlock > 0) {
		m_AttacksToBlock--;

		return;
	}

	// If this entity has damage reduction, reduce the damage to a minimum of 1
	if (m_DamageReduction > 0 && damage > 0) {
		if (damage > m_DamageReduction) {
			damage -= m_DamageReduction;
		} else {
			damage = 1;
		}
	}

	const auto sourceDamage = damage;

	auto absorb = static_cast<uint32_t>(GetDamageToAbsorb());
	auto armor = static_cast<uint32_t>(GetArmor());
	auto health = static_cast<uint32_t>(GetHealth());

	const auto absorbDamage = std::min(damage, absorb);

	damage -= absorbDamage;
	absorb -= absorbDamage;

	const auto armorDamage = std::min(damage, armor);

	damage -= armorDamage;
	armor -= armorDamage;

	health -= std::min(damage, health);

	SetDamageToAbsorb(absorb);
	SetArmor(armor);
	SetHealth(health);
	SetIsShielded(absorb > 0);

	// Dismount on the possessable hit
	auto* possessable = m_ParentEntity->GetComponent<PossessableComponent>();
	if (possessable && possessable->GetDepossessOnHit()) {
		possessable->Dismount();
	}

	// Dismount on the possessor hit
	auto* possessor = m_ParentEntity->GetComponent<PossessorComponent>();
	if (possessor) {
		auto possessableId = possessor->GetPossessable();
		if (possessableId != LWOOBJID_EMPTY) {
			auto possessable = EntityManager::Instance()->GetEntity(possessableId);
			if (possessable) {
				possessor->Dismount(possessable);
			}
		}
	}

	if (m_ParentEntity->GetLOT() != 1) {
		echo = true;
	}

	if (echo) {
		EntityManager::Instance()->SerializeEntity(m_ParentEntity);
	}

	auto* attacker = EntityManager::Instance()->GetEntity(source);
	m_ParentEntity->OnHit(attacker);
	m_ParentEntity->OnHitOrHealResult(attacker, sourceDamage);
	NotifySubscribers(attacker, sourceDamage);

	for (const auto& cb : m_OnHitCallbacks) {
		cb(attacker);
	}

	if (health != 0) {
		auto* combatComponent = m_ParentEntity->GetComponent<BaseCombatAIComponent>();

		if (combatComponent != nullptr) {
			combatComponent->Taunt(source, sourceDamage * 10); // * 10 is arbatrary
		}

		return;
	}

	//check if hardcore mode is enabled
	if (EntityManager::Instance()->GetHardcoreMode()) {
		DoHardcoreModeDrops(source);
	}

	Smash(source, eKillType::VIOLENT, u"", skillID);
}

void DestroyableComponent::Subscribe(CppScripts::Script* scriptToAdd) {
	auto foundScript = std::find(m_SubscribedScripts.begin(), m_SubscribedScripts.end(), scriptToAdd);
	if (foundScript != m_SubscribedScripts.end()) {
		Game::logger->LogDebug("DestroyableComponent", "WARNING: Tried to add a script for Entity %llu but the script was already subscribed", m_ParentEntity->GetObjectID());
		return;
	}
	m_SubscribedScripts.push_back(scriptToAdd);
	Game::logger->LogDebug("DestroyableComponent", "A script has subscribed to entity %llu", m_ParentEntity->GetObjectID());
	Game::logger->LogDebug("DestroyableComponent", "Number of subscribed scripts %i", m_SubscribedScripts.size());
}

void DestroyableComponent::Unsubscribe(CppScripts::Script* scriptToRemove) {
	auto foundScript = std::find(m_SubscribedScripts.begin(), m_SubscribedScripts.end(), scriptToRemove);
	if (foundScript != m_SubscribedScripts.end()) {
		m_SubscribedScripts.erase(foundScript);
		Game::logger->LogDebug("DestroyableComponent", "Unsubscribed a script from entity %llu", m_ParentEntity->GetObjectID());
		Game::logger->LogDebug("DestroyableComponent", "Number of subscribed scripts %i", m_SubscribedScripts.size());
		return;
	}
	Game::logger->LogDebug("DestroyableComponent", "WARNING: Tried to remove a script for Entity %llu but the script was not subscribed", m_ParentEntity->GetObjectID());
}

void DestroyableComponent::NotifySubscribers(Entity* attacker, uint32_t damage) {
	for (auto* script : m_SubscribedScripts) {
		DluAssert(script != nullptr);
		script->NotifyHitOrHealResult(m_ParentEntity, attacker, damage);
	}
}

void DestroyableComponent::Smash(const LWOOBJID source, const eKillType killType, const std::u16string& deathType, uint32_t skillID) {
	if (m_iHealth > 0) {
		SetArmor(0);
		SetHealth(0);

		EntityManager::Instance()->SerializeEntity(m_ParentEntity);
	}

	m_KillerID = source;

	auto* owner = EntityManager::Instance()->GetEntity(source);

	if (owner != nullptr) {
		owner = owner->GetOwner(); // If the owner is overwritten, we collect that here

		auto* team = TeamManager::Instance()->GetTeam(owner->GetObjectID());

		const auto isEnemy = m_ParentEntity->GetComponent<BaseCombatAIComponent>() != nullptr;

		auto* inventoryComponent = owner->GetComponent<InventoryComponent>();

		if (inventoryComponent != nullptr && isEnemy) {
			inventoryComponent->TriggerPassiveAbility(PassiveAbilityTrigger::EnemySmashed, m_ParentEntity);
		}

		auto* missions = owner->GetComponent<MissionComponent>();

		if (missions != nullptr) {
			if (team != nullptr) {
				for (const auto memberId : team->members) {
					auto* member = EntityManager::Instance()->GetEntity(memberId);

					if (member == nullptr) continue;

					auto* memberMissions = member->GetComponent<MissionComponent>();

					if (memberMissions == nullptr) continue;

					memberMissions->Progress(eMissionTaskType::SMASH, m_ParentEntity->GetLOT());
					memberMissions->Progress(eMissionTaskType::USE_SKILL, m_ParentEntity->GetLOT(), skillID);
				}
			} else {
				missions->Progress(eMissionTaskType::SMASH, m_ParentEntity->GetLOT());
				missions->Progress(eMissionTaskType::USE_SKILL, m_ParentEntity->GetLOT(), skillID);
			}
		}
	}

	const auto isPlayer = m_ParentEntity->IsPlayer();

	GameMessages::SendDie(m_ParentEntity, source, source, true, killType, deathType, 0, 0, 0, isPlayer, false, 1);

	//NANI?!
	if (!isPlayer) {
		if (owner != nullptr) {
			auto* team = TeamManager::Instance()->GetTeam(owner->GetObjectID());

			if (team != nullptr && m_ParentEntity->GetComponent<BaseCombatAIComponent>() != nullptr) {
				LWOOBJID specificOwner = LWOOBJID_EMPTY;
				auto* scriptedActivityComponent = m_ParentEntity->GetComponent<ScriptedActivityComponent>();
				uint32_t teamSize = team->members.size();
				uint32_t lootMatrixId = GetLootMatrixID();

				if (scriptedActivityComponent) {
					lootMatrixId = scriptedActivityComponent->GetLootMatrixForTeamSize(teamSize);
				}

				if (team->lootOption == 0) { // Round robin
					specificOwner = TeamManager::Instance()->GetNextLootOwner(team);

					auto* member = EntityManager::Instance()->GetEntity(specificOwner);

					if (member) LootGenerator::Instance().DropLoot(member, m_ParentEntity, lootMatrixId, GetMinCoins(), GetMaxCoins());
				} else {
					for (const auto memberId : team->members) { // Free for all
						auto* member = EntityManager::Instance()->GetEntity(memberId);

						if (member == nullptr) continue;

						LootGenerator::Instance().DropLoot(member, m_ParentEntity, lootMatrixId, GetMinCoins(), GetMaxCoins());
					}
				}
			} else { // drop loot for non team user
				LootGenerator::Instance().DropLoot(owner, m_ParentEntity, GetLootMatrixID(), GetMinCoins(), GetMaxCoins());
			}
		}
	} else {
		//Check if this zone allows coin drops
		if (dZoneManager::Instance()->GetPlayerLoseCoinOnDeath()) {
			auto* character = m_ParentEntity->GetCharacter();
			uint64_t coinsTotal = character->GetCoins();
			const uint64_t minCoinsToLose = dZoneManager::Instance()->GetWorldConfig()->coinsLostOnDeathMin;
			if (coinsTotal >= minCoinsToLose) {
				const uint64_t maxCoinsToLose = dZoneManager::Instance()->GetWorldConfig()->coinsLostOnDeathMax;
				const float coinPercentageToLose = dZoneManager::Instance()->GetWorldConfig()->coinsLostOnDeathPercent;

				uint64_t coinsToLose = std::max(static_cast<uint64_t>(coinsTotal * coinPercentageToLose), minCoinsToLose);
				coinsToLose = std::min(maxCoinsToLose, coinsToLose);

				coinsTotal -= coinsToLose;

				LootGenerator::Instance().DropLoot(m_ParentEntity, m_ParentEntity, -1, coinsToLose, coinsToLose);
				character->SetCoins(coinsTotal, eLootSourceType::PICKUP);
			}
		}

		Entity* zoneControl = EntityManager::Instance()->GetZoneControlEntity();
		zoneControl->GetScript()->OnPlayerDied(zoneControl, m_ParentEntity);

		std::vector<Entity*> scriptedActs = EntityManager::Instance()->GetEntitiesByComponent(eReplicaComponentType::SCRIPTED_ACTIVITY);
		for (Entity* scriptEntity : scriptedActs) {
			if (scriptEntity->GetObjectID() != zoneControl->GetObjectID()) { // Don't want to trigger twice on instance worlds
				scriptEntity->GetScript()->OnPlayerDied(scriptEntity, m_ParentEntity);
			}
		}
	}

	m_ParentEntity->Kill(owner);
}

void DestroyableComponent::SetFaction(int32_t factionID, bool ignoreChecks) {
	m_FactionIDs.clear();
	m_EnemyFactionIDs.clear();

	AddFaction(factionID, ignoreChecks);
}

void DestroyableComponent::SetStatusImmunity(
	const eStateChangeType state,
	const bool bImmuneToBasicAttack,
	const bool bImmuneToDamageOverTime,
	const bool bImmuneToKnockback,
	const bool bImmuneToInterrupt,
	const bool bImmuneToSpeed,
	const bool bImmuneToImaginationGain,
	const bool bImmuneToImaginationLoss,
	const bool bImmuneToQuickbuildInterrupt,
	const bool bImmuneToPullToPoint) {

	if (state == eStateChangeType::POP) {
		if (bImmuneToBasicAttack && m_ImmuneToBasicAttackCount > 0) 				m_ImmuneToBasicAttackCount -= 1;
		if (bImmuneToDamageOverTime && m_ImmuneToDamageOverTimeCount > 0) 			m_ImmuneToDamageOverTimeCount -= 1;
		if (bImmuneToKnockback && m_ImmuneToKnockbackCount > 0) 					m_ImmuneToKnockbackCount -= 1;
		if (bImmuneToInterrupt && m_ImmuneToInterruptCount > 0) 					m_ImmuneToInterruptCount -= 1;
		if (bImmuneToSpeed && m_ImmuneToSpeedCount > 0) 							m_ImmuneToSpeedCount -= 1;
		if (bImmuneToImaginationGain && m_ImmuneToImaginationGainCount > 0) 		m_ImmuneToImaginationGainCount -= 1;
		if (bImmuneToImaginationLoss && m_ImmuneToImaginationLossCount > 0) 		m_ImmuneToImaginationLossCount -= 1;
		if (bImmuneToQuickbuildInterrupt && m_ImmuneToQuickbuildInterruptCount > 0) m_ImmuneToQuickbuildInterruptCount -= 1;
		if (bImmuneToPullToPoint && m_ImmuneToPullToPointCount > 0) 				m_ImmuneToPullToPointCount -= 1;

	} else if (state == eStateChangeType::PUSH) {
		if (bImmuneToBasicAttack) 			m_ImmuneToBasicAttackCount += 1;
		if (bImmuneToDamageOverTime) 		m_ImmuneToDamageOverTimeCount += 1;
		if (bImmuneToKnockback) 			m_ImmuneToKnockbackCount += 1;
		if (bImmuneToInterrupt) 			m_ImmuneToInterruptCount += 1;
		if (bImmuneToSpeed) 				m_ImmuneToSpeedCount += 1;
		if (bImmuneToImaginationGain) 		m_ImmuneToImaginationGainCount += 1;
		if (bImmuneToImaginationLoss) 		m_ImmuneToImaginationLossCount += 1;
		if (bImmuneToQuickbuildInterrupt) 	m_ImmuneToQuickbuildInterruptCount += 1;
		if (bImmuneToPullToPoint) 			m_ImmuneToPullToPointCount += 1;
	}

	GameMessages::SendSetStatusImmunity(
		m_ParentEntity->GetObjectID(), state, m_ParentEntity->GetSystemAddress(),
		bImmuneToBasicAttack,
		bImmuneToDamageOverTime,
		bImmuneToKnockback,
		bImmuneToInterrupt,
		bImmuneToSpeed,
		bImmuneToImaginationGain,
		bImmuneToImaginationLoss,
		bImmuneToQuickbuildInterrupt,
		bImmuneToPullToPoint
	);
}

void DestroyableComponent::FixStats() {
	auto* entity = GetParentEntity();

	if (entity == nullptr) return;

	// Reset skill component and buff component
	auto* skillComponent = entity->GetComponent<SkillComponent>();
	auto* buffComponent = entity->GetComponent<BuffComponent>();
	auto* missionComponent = entity->GetComponent<MissionComponent>();
	auto* inventoryComponent = entity->GetComponent<InventoryComponent>();
	auto* destroyableComponent = entity->GetComponent<DestroyableComponent>();

	// If any of the components are nullptr, return
	if (skillComponent == nullptr || buffComponent == nullptr || missionComponent == nullptr || inventoryComponent == nullptr || destroyableComponent == nullptr) {
		return;
	}

	// Save the current stats
	int32_t currentHealth = destroyableComponent->GetHealth();
	int32_t currentArmor = destroyableComponent->GetArmor();
	int32_t currentImagination = destroyableComponent->GetImagination();

	// Unequip all items
	auto equipped = inventoryComponent->GetEquippedItems();

	for (auto& equippedItem : equipped) {
		// Get the item with the item ID
		auto* item = inventoryComponent->FindItemById(equippedItem.second.id);

		if (item == nullptr) {
			continue;
		}

		// Unequip the item
		item->UnEquip();
	}

	// Base stats
	int32_t maxHealth = 4;
	int32_t maxArmor = 0;
	int32_t maxImagination = 0;

	// Go through all completed missions and add the reward stats
	for (auto& pair : missionComponent->GetMissions()) {
		auto* mission = pair.second;

		if (!mission->IsComplete()) {
			continue;
		}

		// Add the stats
		const auto& info = mission->GetClientInfo();

		maxHealth += info.reward_maxhealth;
		maxImagination += info.reward_maximagination;
	}

	// Set the base stats
	destroyableComponent->SetMaxHealth(maxHealth);
	destroyableComponent->SetMaxArmor(maxArmor);
	destroyableComponent->SetMaxImagination(maxImagination);

	// Re-apply all buffs
	buffComponent->ReApplyBuffs();

	// Requip all items
	for (auto& equippedItem : equipped) {
		// Get the item with the item ID
		auto* item = inventoryComponent->FindItemById(equippedItem.second.id);

		if (item == nullptr) {
			continue;
		}

		// Equip the item
		item->Equip();
	}

	// Fetch correct max stats after everything is done
	maxHealth = destroyableComponent->GetMaxHealth();
	maxArmor = destroyableComponent->GetMaxArmor();
	maxImagination = destroyableComponent->GetMaxImagination();

	// If any of the current stats are more than their max, set them to the max
	if (currentHealth > maxHealth) currentHealth = maxHealth;
	if (currentArmor > maxArmor) currentArmor = maxArmor;
	if (currentImagination > maxImagination) currentImagination = maxImagination;

	// Restore current stats
	destroyableComponent->SetHealth(currentHealth);
	destroyableComponent->SetArmor(currentArmor);
	destroyableComponent->SetImagination(currentImagination);

	// Serialize the entity
	EntityManager::Instance()->SerializeEntity(entity);
}

void DestroyableComponent::AddOnHitCallback(const std::function<void(Entity*)>& callback) {
	m_OnHitCallbacks.push_back(callback);
}

void DestroyableComponent::DoHardcoreModeDrops(const LWOOBJID source) {
	//check if this is a player:
	if (m_ParentEntity->IsPlayer()) {
		//remove hardcore_lose_uscore_on_death_percent from the player's uscore:
		auto* character = m_ParentEntity->GetComponent<CharacterComponent>();
		auto uscore = character->GetUScore();

		auto uscoreToLose = uscore * (EntityManager::Instance()->GetHardcoreLoseUscoreOnDeathPercent() / 100);
		character->SetUScore(uscore - uscoreToLose);

		GameMessages::SendModifyLEGOScore(m_ParentEntity, m_ParentEntity->GetSystemAddress(), -uscoreToLose, eLootSourceType::MISSION);

		if (EntityManager::Instance()->GetHardcoreDropinventoryOnDeath()) {
			//drop all items from inventory:
			auto* inventory = m_ParentEntity->GetComponent<InventoryComponent>();
			if (inventory) {
				//get the items inventory:
				auto items = inventory->GetInventory(eInventoryType::ITEMS);
				if (items) {
					auto itemMap = items->GetItems();
					if (!itemMap.empty()) {
						for (const auto& item : itemMap) {
							//drop the item:
							if (!item.second) continue;
							// don't drop the thinkng cap
							if (item.second->GetLot() == 6086) continue;
							GameMessages::SendDropClientLoot(m_ParentEntity, source, item.second->GetLot(), 0, m_ParentEntity->GetPosition(), item.second->GetCount());
							item.second->SetCount(0, false, false);
						}
						EntityManager::Instance()->SerializeEntity(m_ParentEntity);
					}
				}
			}
		}

		//get character:
		auto* chars = m_ParentEntity->GetCharacter();
		if (chars) {
			auto coins = chars->GetCoins();

			//lose all coins:
			chars->SetCoins(0, eLootSourceType::NONE);

			//drop all coins:
			GameMessages::SendDropClientLoot(m_ParentEntity, source, LOT_NULL, coins, m_ParentEntity->GetPosition());
		}

		// Reload the player since we can't normally reduce uscore from the server and we want the UI to update
		// do this last so we don't get killed.... again
		EntityManager::Instance()->DestructEntity(m_ParentEntity);
		EntityManager::Instance()->ConstructEntity(m_ParentEntity);
		return;
	}

	//award the player some u-score:
	auto* player = EntityManager::Instance()->GetEntity(source);
	if (player && player->IsPlayer()) {
		auto* playerStats = player->GetComponent<CharacterComponent>();
		if (playerStats) {
			//get the maximum health from this enemy:
			auto maxHealth = GetMaxHealth();

			int uscore = maxHealth * EntityManager::Instance()->GetHardcoreUscoreEnemiesMultiplier();

			playerStats->SetUScore(playerStats->GetUScore() + uscore);
			GameMessages::SendModifyLEGOScore(player, player->GetSystemAddress(), uscore, eLootSourceType::MISSION);

			EntityManager::Instance()->SerializeEntity(m_ParentEntity);
		}
	}
}
