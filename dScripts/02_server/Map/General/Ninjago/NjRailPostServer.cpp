#include "NjRailPostServer.h"
#include "QuickBuildComponent.h"
#include "EntityManager.h"
#include "Entity.h"

void NjRailPostServer::OnStartup(Entity* self) {
	auto* quickBuildComponent = self->GetComponent<QuickBuildComponent>();
	if (quickBuildComponent != nullptr) {
		self->SetNetworkVar<bool>(NetworkNotActiveVariable, true);
	}
}

void NjRailPostServer::OnNotifyObject(Entity* self, Entity* sender, const std::u16string& name, int32_t param1,
	int32_t param2) {
	if (name == u"PostRebuilt") {
		self->SetNetworkVar<bool>(NetworkNotActiveVariable, false);
	} else if (name == u"PostDied") {
		self->SetNetworkVar<bool>(NetworkNotActiveVariable, true);
	}
}

void NjRailPostServer::OnRebuildNotifyState(Entity* self, eRebuildState state) {
	if (state == eRebuildState::COMPLETED) {
		auto* relatedRail = GetRelatedRail(self);
		if (relatedRail == nullptr)
			return;

		relatedRail->NotifyObject(self, u"PostRebuilt");

		if (self->GetVar<bool>(NotActiveVariable))
			return;

		self->SetNetworkVar(NetworkNotActiveVariable, false);
	} else if (state == eRebuildState::RESETTING) {
		auto* relatedRail = GetRelatedRail(self);
		if (relatedRail == nullptr)
			return;

		relatedRail->NotifyObject(self, u"PostDied");
	}
}

Entity* NjRailPostServer::GetRelatedRail(Entity* self) {
	const auto& railGroup = self->GetVar<std::u16string>(RailGroupVariable);
	if (!railGroup.empty()) {
		for (auto* entity : EntityManager::Instance()->GetEntitiesInGroup(GeneralUtils::UTF16ToWTF8(railGroup))) {
			return entity;
		}
	}

	return nullptr;
}
