#include "PossessorComponent.h"
#include "PossessableComponent.h"
#include "CharacterComponent.h"
#include "EntityManager.h"
#include "GameMessages.h"
#include "eUnequippableActiveType.h"
#include "eControlScheme.h"
#include "eStateChangeType.h"

PossessorComponent::PossessorComponent(Entity* parent) : Component(parent) {
	m_Possessable = LWOOBJID_EMPTY;
}

PossessorComponent::~PossessorComponent() {
	if (m_Possessable != LWOOBJID_EMPTY) {
		auto* mount = EntityManager::Instance()->GetEntity(m_Possessable);
		if (mount) {
			auto* possessable = mount->GetComponent<PossessableComponent>();
			if (possessable) {
				if (possessable->GetIsItemSpawned()) {
					GameMessages::SendMarkInventoryItemAsActive(m_OwningEntity->GetObjectID(), false, eUnequippableActiveType::MOUNT, GetMountItemID(), m_OwningEntity->GetSystemAddress());
				}
				possessable->Dismount();
			}
		}
	}
}

void PossessorComponent::Serialize(RakNet::BitStream* outBitStream, bool bIsInitialUpdate, unsigned int& flags) {
	outBitStream->Write(m_DirtyPossesor || bIsInitialUpdate);
	if (m_DirtyPossesor || bIsInitialUpdate) {
		m_DirtyPossesor = false;
		outBitStream->Write(m_Possessable != LWOOBJID_EMPTY);
		if (m_Possessable != LWOOBJID_EMPTY) {
			outBitStream->Write(m_Possessable);
		}
		outBitStream->Write(m_PossessableType);
	}
}

void PossessorComponent::Mount(Entity* mount) {
	// Don't do anything if we are busy dismounting
	if (GetIsDismounting() || !mount) return;

	GameMessages::SendSetMountInventoryID(m_OwningEntity, mount->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS);
	auto* possessableComponent = mount->GetComponent<PossessableComponent>();
	if (possessableComponent) {
		possessableComponent->SetPossessor(m_OwningEntity->GetObjectID());
		SetPossessable(mount->GetObjectID());
		SetPossessableType(possessableComponent->GetPossessionType());
	}

	auto* characterComponent = m_OwningEntity->GetComponent<CharacterComponent>();
	if (characterComponent) characterComponent->SetIsRacing(true);

	// GM's to send
	GameMessages::SendSetJetPackMode(m_OwningEntity, false);
	GameMessages::SendVehicleUnlockInput(mount->GetObjectID(), false, m_OwningEntity->GetSystemAddress());
	GameMessages::SendSetStunned(m_OwningEntity->GetObjectID(), eStateChangeType::PUSH, m_OwningEntity->GetSystemAddress(), LWOOBJID_EMPTY, true, false, true, false, false, false, false, true, true, true, true, true, true, true, true, true);

	EntityManager::Instance()->SerializeEntity(m_OwningEntity);
	EntityManager::Instance()->SerializeEntity(mount);
}

void PossessorComponent::Dismount(Entity* mount, bool forceDismount) {
	// Don't do anything if we are busy dismounting
	if (GetIsDismounting() || !mount) return;
	SetIsDismounting(true);

	if (mount) {
		auto* possessableComponent = mount->GetComponent<PossessableComponent>();
		if (possessableComponent) {
			possessableComponent->SetPossessor(LWOOBJID_EMPTY);
			if (forceDismount) possessableComponent->ForceDepossess();
		}
		EntityManager::Instance()->SerializeEntity(m_OwningEntity);
		EntityManager::Instance()->SerializeEntity(mount);

		auto* characterComponent = m_OwningEntity->GetComponent<CharacterComponent>();
		if (characterComponent) characterComponent->SetIsRacing(false);
	}
	// Make sure we don't have wacky controls
	GameMessages::SendSetPlayerControlScheme(m_OwningEntity, eControlScheme::SCHEME_A);
}
