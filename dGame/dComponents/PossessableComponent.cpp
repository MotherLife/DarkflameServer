#include "PossessableComponent.h"
#include "PossessionComponent.h"
#include "EntityManager.h"
#include "Inventory.h"
#include "Item.h"

PossessableComponent::PossessableComponent(Entity* parent, uint32_t componentId) : Component(parent) {
	m_Possessor = LWOOBJID_EMPTY;
	m_ComponentId = componentId;
}

void PossessableComponent::LoadTemplateData() {
	auto item = Inventory::FindItemComponent(m_ParentEntity->GetLOT());
	m_AnimationFlag = static_cast<eAnimationFlags>(item.animationFlag);

	// Get the possession Type from the CDClient
	auto query = CDClientDatabase::CreatePreppedStmt("SELECT possessionType, depossessOnHit FROM PossessableComponent WHERE id = ?;");

	query.bind(1, static_cast<int32_t>(m_ComponentId));

	auto result = query.execQuery();

	// Should a result not exist for this default to attached visible
	if (!result.eof()) {
		m_PossessionType = static_cast<ePossessionType>(result.getIntField(0, 1)); // Default to Attached Visible
		m_DepossessOnHit = static_cast<bool>(result.getIntField(1, 0));
	} else {
		m_PossessionType = ePossessionType::ATTACHED_VISIBLE;
		m_DepossessOnHit = false;
	}
}

void PossessableComponent::Serialize(RakNet::BitStream* outBitStream, bool bIsInitialUpdate, unsigned int& flags) {
	outBitStream->Write(m_DirtyPossessable || bIsInitialUpdate);
	if (m_DirtyPossessable || bIsInitialUpdate) {
		outBitStream->Write(m_Possessor != LWOOBJID_EMPTY);
		if (m_Possessor != LWOOBJID_EMPTY) outBitStream->Write(m_Possessor);

		outBitStream->Write(m_AnimationFlag != eAnimationFlags::IDLE_NONE);
		if (m_AnimationFlag != eAnimationFlags::IDLE_NONE) outBitStream->Write(m_AnimationFlag);

		outBitStream->Write(m_ImmediatelyDepossess);
		if (!bIsInitialUpdate) {
			m_DirtyPossessable = false;
			m_ImmediatelyDepossess = false;
		}
	}
}

void PossessableComponent::Dismount() {
	SetPossessor(LWOOBJID_EMPTY);
	if (m_ItemSpawned) m_ParentEntity->ScheduleKillAfterUpdate();
}

void PossessableComponent::OnUse(Entity* originator) {
	auto* possessor = originator->GetComponent<PossessionComponent>();
	if (possessor) {
		possessor->Mount(m_ParentEntity);
	}
}
