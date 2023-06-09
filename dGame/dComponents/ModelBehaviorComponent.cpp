#include "ModelBehaviorComponent.h"
#include "Entity.h"

ModelBehaviorComponent::ModelBehaviorComponent(Entity* parent) : Component(parent) {
	m_OriginalPosition = m_ParentEntity->GetDefaultPosition();
	m_OriginalRotation = m_ParentEntity->GetDefaultRotation();

	m_userModelID = m_ParentEntity->GetVarAs<LWOOBJID>(u"userModelID");
}

void ModelBehaviorComponent::Serialize(RakNet::BitStream* outBitStream, bool bIsInitialUpdate, unsigned int& flags) {
	// ItemComponent Serialization.  Pets do not get this serialization.
	if (!m_ParentEntity->HasComponent(eReplicaComponentType::PET)) {
		outBitStream->Write1();
		outBitStream->Write<LWOOBJID>(m_userModelID != LWOOBJID_EMPTY ? m_userModelID : m_ParentEntity->GetObjectID());
		outBitStream->Write<int>(0);
		outBitStream->Write0();
	}

	//actual model component:
	outBitStream->Write1(); // Yes we are writing model info
	outBitStream->Write0(); // Is pickable
	outBitStream->Write<uint32_t>(2); // Physics type
	outBitStream->Write(m_OriginalPosition); // Original position
	outBitStream->Write(m_OriginalRotation); // Original rotation

	outBitStream->Write1(); // We are writing behavior info
	outBitStream->Write<uint32_t>(0); // Number of behaviors
	outBitStream->Write1(); // Is this model paused
	if (bIsInitialUpdate) outBitStream->Write0(); // We are not writing model editing info
}
