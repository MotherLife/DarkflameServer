#include "HealBehavior.h"
#include "BehaviorBranchContext.h"
#include "Game.h"
#include "dLogger.h"
#include "EntityManager.h"
#include "DestroyableComponent.h"
#include "eReplicaComponentType.h"
#include "LevelProgressionComponent.h"


void HealBehavior::Handle(BehaviorContext* context, RakNet::BitStream* bit_stream, const BehaviorBranchContext branch) {
	auto* entity = EntityManager::Instance()->GetEntity(branch.target);

	if (entity == nullptr) {
		Game::logger->Log("HealBehavior", "Failed to find entity for (%llu)!", branch.target);

		return;
	}

	auto* destroyable = static_cast<DestroyableComponent*>(entity->GetComponent(eReplicaComponentType::DESTROYABLE));

	if (destroyable == nullptr) {
		Game::logger->Log("HealBehavior", "Failed to find destroyable component for %(llu)!", branch.target);

		return;
	}

	int32_t toApply = this->m_health * 5;

	auto* levelProgressComponent = entity->GetComponent<LevelProgressionComponent>();

	if (levelProgressComponent != nullptr) {
		toApply *= levelProgressComponent->GetLevel();
	}

	// Apply a standard deviations of 20%
	toApply = static_cast<uint32_t>(toApply * (1.0f + (static_cast<float>(rand() % 40) / 100.0f) - 0.2f));

	destroyable->Heal(toApply);
}


void HealBehavior::Calculate(BehaviorContext* context, RakNet::BitStream* bit_stream, const BehaviorBranchContext branch) {
	Handle(context, bit_stream, branch);
}


void HealBehavior::Load() {
	this->m_health = GetInt("health");
}
