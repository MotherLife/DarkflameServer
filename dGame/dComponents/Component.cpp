#include "Component.h"
#include "DluAssert.h"

Component::Component(Entity* owningEntity) {
	DluAssert(owningEntity != nullptr);
	m_ParentEntity = owningEntity;
}

Component::~Component() {

}

void Component::Update(float deltaTime) {

}

void Component::OnUse(Entity* originator) {

}

void Component::UpdateXml(tinyxml2::XMLDocument* doc) {

}

void Component::LoadFromXml(tinyxml2::XMLDocument* doc) {

}

void Component::Startup() {

}

void Component::LoadConfigData() {

}

void Component::LoadTemplateData() {

}

void Component::Serialize(RakNet::BitStream* bitStream, bool isConstruction) {

}
