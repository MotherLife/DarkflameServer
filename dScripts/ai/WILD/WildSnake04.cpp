#include "WildSnake04.h"
#include "EntityManager.h"
#include "DestroyableComponent.h"
#include "RenderComponent.h"

void WildSnake04::OnStartup(Entity* self) {
	self->AddTimer("SnakeStartup",0.2f);
	self->AddTimer("SnakeFollow",0.3f);
}

void WildSnake04::OnNotifyObject(Entity* self, Entity* sender, const std::string& name, int32_t param1, int32_t param2) {
	if (name == "next1"){
		auto* destroyableComponent = self->GetComponent<DestroyableComponent>();
		if (destroyableComponent) destroyableComponent->SetFaction(4);
		auto* renderComponent = self->GetComponent<RenderComponent>();
		if (renderComponent) renderComponent->PlayEffect(634, u"yellow", "");
	}
}

void WildSnake04::OnTimerDone(Entity* self, std::string timerName) {
	if (timerName == "SnakeStartup"){
		self->AddToGroup("snakesonaplane");
	} else if (timerName == "SnakeFollow") {
		auto snakes = Game::entityManager->GetEntitiesInGroup("snakesonaplane");
		for (const auto &snake : snakes) {
			if (snake && snake->GetLOT() == 6566){
				// self:FollowTarget { targetID = friends[i], radius = 4.5, speed = 1, keepFollowing = true }
			}
		}
	}
}

void WildSnake04::OnDie(Entity* self, Entity* killer) {
	auto snakes = Game::entityManager->GetEntitiesInGroup("snakesonaplane");
	for (const auto &snake : snakes) {
		if (snake) snake->NotifyObject(self, "next2");
	}
}
