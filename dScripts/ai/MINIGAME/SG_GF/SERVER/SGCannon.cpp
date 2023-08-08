#include "SGCannon.h"
#include "EntityManager.h"
#include "GameMessages.h"
#include "dZoneManager.h"
#include "Player.h"
#include "Character.h"
#include "ShootingGalleryComponent.h"
#include "PossessorComponent.h"
#include "CharacterComponent.h"
#include "SimplePhysicsComponent.h"
#include "MovementAIComponent.h"
#include "../dWorldServer/ObjectIDManager.h"
#include "MissionComponent.h"
#include "Loot.h"
#include "InventoryComponent.h"
#include "eMissionTaskType.h"
#include "eReplicaComponentType.h"
#include "RenderComponent.h"
#include "eGameActivity.h"

void SGCannon::OnStartup(Entity* self) {
	Game::logger->Log("SGCannon", "OnStartup");

	m_Waves = GetWaves();
	constants = GetConstants();

	ResetVars(self);

	self->SetVar<bool>(GameStartedVariable, false);
	self->SetVar<Vector3>(InitialVelocityVariable, {});
	self->SetVar<uint32_t>(ImpactSkillVariale, constants.impactSkillID);

	auto* shootingGalleryComponent = self->GetComponent<ShootingGalleryComponent>();
	if (shootingGalleryComponent != nullptr) {
		shootingGalleryComponent->SetStaticParams({
			Vector3 { -327.8609924316406, 256.8999938964844, 1.6482199430465698 },
			Vector3 { -181.4320068359375, 212.39999389648438, 2.5182199478149414 }
			});

		shootingGalleryComponent->SetDynamicParams({
			Vector3 { 0.0, 4.3, 9.0 },
			Vector3 { },
			129.0,
			800.0,
			30.0,
			0.0,
			-1.0,
			58.6
			});
	}

	self->SetVar<uint32_t>(TimeLimitVariable, 30);
	self->SetVar<std::vector<LOT>>(ValidActorsVariable, { 3109, 3110, 3111, 3112, 3125, 3126 });
	self->SetVar<std::vector<LOT>>(ValidEffectsVariable, { 3122 });
	self->SetVar<std::vector<uint32_t>>(StreakBonusVariable, { 1, 2, 5, 10 });
	self->SetVar<bool>(SuperChargeActiveVariable, false);
	self->SetVar<uint32_t>(MatrixVariable, 1);
	self->SetVar<bool>(InitVariable, true);

	auto* simplePhysicsComponent = self->GetComponent<SimplePhysicsComponent>();

	if (simplePhysicsComponent != nullptr) {
		simplePhysicsComponent->SetPhysicsMotionState(5);
	}
}

void SGCannon::OnPlayerLoaded(Entity* self, Entity* player) {
	Game::logger->Log("SGCannon", "Player loaded");
	self->SetVar<LWOOBJID>(PlayerIDVariable, player->GetObjectID());
}

void SGCannon::OnFireEventServerSide(Entity* self, Entity* sender, std::string args, int32_t param1, int32_t param2,
	int32_t param3) {
	Script::OnFireEventServerSide(self, sender, args, param1, param2, param3);
}

void SGCannon::OnActivityStateChangeRequest(Entity* self, LWOOBJID senderID, int32_t value1, int32_t value2,
	const std::u16string& stringValue) {
	Game::logger->Log("SGCannon", "Got activity state change request: %s", GeneralUtils::UTF16ToWTF8(stringValue).c_str());
	if (stringValue == u"clientready") {
		auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
		if (player != nullptr) {
			Game::logger->Log("SGCannon", "Player is ready");
			/*GameMessages::SendSetStunned(player->GetObjectID(), eStateChangeType::PUSH, player->GetSystemAddress(), LWOOBJID_EMPTY,
										 true, true, true, true, true, true, true);*/

			Game::logger->Log("SGCannon", "Sending ActivityEnter");

			GameMessages::SendActivityEnter(self->GetObjectID(), player->GetSystemAddress());

			auto* shootingGalleryComponent = self->GetComponent<ShootingGalleryComponent>();

			if (shootingGalleryComponent != nullptr) {
				shootingGalleryComponent->SetCurrentPlayerID(player->GetObjectID());

				Game::logger->Log("SGCannon", "Setting player ID");

				Game::entityManager->SerializeEntity(self);
			} else {
				Game::logger->Log("SGCannon", "Shooting gallery component is null");
			}

			auto* characterComponent = player->GetComponent<CharacterComponent>();

			if (characterComponent != nullptr) {
				characterComponent->SetIsRacing(true);
				characterComponent->SetCurrentActivity(eGameActivity::SHOOTING_GALLERY);
				auto possessor = player->GetComponent<PossessorComponent>();
				if (possessor) {
					possessor->SetPossessable(self->GetObjectID());
					possessor->SetPossessableType(ePossessionType::NO_POSSESSION);
				}

				Game::entityManager->SerializeEntity(player);
			}

			self->SetNetworkVar<bool>(HideScoreBoardVariable, true);
			self->SetNetworkVar<bool>(ReSetSuperChargeVariable, true);
			self->SetNetworkVar<bool>(ShowLoadingUI, true);

			/*
			GameMessages::SendTeleport(
				player->GetObjectID(),
				{-292.6415710449219, 230.20237731933594, -3.9090466499328613},
				{0.7067984342575073, -6.527870573336259e-05, 0.707414984703064, 0.00021762956748716533},
				player->GetSystemAddress(), true
			);
			*/

			//GameMessages::SendRequestActivityEnter(self->GetObjectID(), player->GetSystemAddress(), false, player->GetObjectID());
		} else {
			Game::logger->Log("SGCannon", "Player not found");
		}
	} else if (value1 == 1200) {
		StartGame(self);
	}
}

void SGCannon::OnMessageBoxResponse(Entity* self, Entity* sender, int32_t button, const std::u16string& identifier, const std::u16string& userData) {
	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
	if (!player) return;

	if (identifier == u"Scoreboardinfo") {
		GameMessages::SendDisplayMessageBox(player->GetObjectID(), true,
			Game::zoneManager->GetZoneControlObject()->GetObjectID(),
			u"Shooting_Gallery_Retry", 2, u"Retry?",
			u"", player->GetSystemAddress());
	} else {
		if ((button == 1 && (identifier == u"Shooting_Gallery_Retry" || identifier == u"RePlay")) || identifier == u"SG1" || button == 0) {
			if (IsPlayerInActivity(self, player->GetObjectID())) return;
			self->SetNetworkVar<bool>(ClearVariable, true);
			StartGame(self);
		} else if (button == 0 && ((identifier == u"Shooting_Gallery_Retry" || identifier == u"RePlay"))) {
			RemovePlayer(player->GetObjectID());
			UpdatePlayer(self, player->GetObjectID(), true);
		} else if (button == 1 && identifier == u"Shooting_Gallery_Exit") {
			UpdatePlayer(self, player->GetObjectID(), true);
			RemovePlayer(player->GetObjectID());
		}
	}
}

void SGCannon::OnActivityTimerDone(Entity* self, const std::string& name) {
	if (name == SuperChargeTimer && !self->GetVar<bool>(SuperChargePausedVariable)) {
		if (self->GetVar<bool>(WaveStatusVariable) || self->GetVar<uint32_t>(CurrentSuperChargedTimeVariable) < 1) {
			self->SetNetworkVar<uint32_t>(ChargeCountingVariable, 99);
			self->SetNetworkVar<uint32_t>(SuperChargeBarVariable, 0);
			ToggleSuperCharge(self, false);
		}
	} else if (name == SpawnWaveTimer) {
		if (self->GetVar<bool>(GameStartedVariable)) {
			self->SetVar<bool>(WaveStatusVariable, true);
			const auto wave = (int32_t)self->GetVar<uint32_t>(ThisWaveVariable);

			if (wave != 0 && self->GetVar<bool>(SuperChargePausedVariable)) {
				StartChargedCannon(self, self->GetVar<uint32_t>(CurrentSuperChargedTimeVariable));
				self->SetVar<uint32_t>(CurrentSuperChargedTimeVariable, 0);
			}

			TimerToggle(self, true);

			for (const auto& enemyToSpawn : m_Waves.at(self->GetVar<uint32_t>(ThisWaveVariable))) {
				SpawnObject(self, enemyToSpawn, true);
			}

			Game::logger->Log("SGCannon", "Current wave spawn: %i/%i", wave, m_Waves.size());

			// All waves completed
			const auto timeLimit = (float_t)self->GetVar<uint32_t>(TimeLimitVariable);
			if (wave >= m_Waves.size()) {
				ActivityTimerStart(self, GameOverTimer, timeLimit, timeLimit);
			} else {
				ActivityTimerStart(self, EndWaveTimer, timeLimit, timeLimit);
			}

			const auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
			if (player != nullptr) {
				GameMessages::SendPlayFXEffect(player->GetObjectID(), -1, u"SG-start", "");

				GameMessages::SendStartActivityTime(self->GetObjectID(), timeLimit, player->GetSystemAddress());
				Game::logger->Log("SGCannon", "Sending ActivityPause false");

				GameMessages::SendActivityPause(self->GetObjectID(), false, player->GetSystemAddress());
			}
		}
	} else if (name == EndWaveTimer) {
		self->SetVar<bool>(WaveStatusVariable, false);
		TimerToggle(self);
		RecordPlayerScore(self);

		if (self->GetVar<uint32_t>(ThisWaveVariable) >= 2) {
			GameMessages::SendActivityPause(self->GetObjectID(), true);
			ActivityTimerStart(self, GameOverTimer, 0.1, 0.1);
			return;
		}

		self->SetVar<uint32_t>(ThisWaveVariable, self->GetVar<uint32_t>(ThisWaveVariable) + 1);
		PlaySceneAnimation(self, u"wave" + GeneralUtils::to_u16string(self->GetVar<uint32_t>(ThisWaveVariable)), true, true, 1.7f);
		self->SetNetworkVar<uint32_t>(WaveNumVariable, self->GetVar<uint32_t>(ThisWaveVariable) + 1);
		self->SetNetworkVar<uint32_t>(WaveStrVariable, self->GetVar<uint32_t>(TimeLimitVariable));

		Game::logger->Log("SGCannon", "Current wave: %i/%i", self->GetVar<uint32_t>(ThisWaveVariable), m_Waves.size());

		if (self->GetVar<uint32_t>(ThisWaveVariable) >= m_Waves.size()) {
			ActivityTimerStart(self, GameOverTimer, 0.1, 0.1);
		} else {
			ActivityTimerStart(self, SpawnWaveTimer, constants.inBetweenWavePause, constants.inBetweenWavePause);
		}

		Game::logger->Log("SGCannon", "Sending ActivityPause true");

		GameMessages::SendActivityPause(self->GetObjectID(), true);
		if (self->GetVar<bool>(SuperChargeActiveVariable) && !self->GetVar<bool>(SuperChargePausedVariable)) {
			PauseChargeCannon(self);
		}
	} else if (name == GameOverTimer) {
		auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
		if (player != nullptr) {
			Game::logger->Log("SGCannon", "Sending ActivityPause true");

			GameMessages::SendActivityPause(self->GetObjectID(), true, player->GetSystemAddress());

			/*const auto leftoverCannonballs = Game::entityManager->GetEntitiesInGroup("cannonball");
			if (leftoverCannonballs.empty()) {
				RecordPlayerScore(self);

			} else {
				ActivityTimerStart(self, EndGameBufferTimer, 1, leftoverCannonballs.size());
			}*/

			ActivityTimerStart(self, EndGameBufferTimer, 1, 1);

			TimerToggle(self);
		}
	} else if (name.rfind(DoSpawnTimer, 0) == 0) {
		if (self->GetVar<bool>(GameStartedVariable)) {
			const auto spawnNumber = (uint32_t)std::stoi(name.substr(7));
			const auto& activeSpawns = self->GetVar<std::vector<SGEnemy>>(ActiveSpawnsVariable);
			if (activeSpawns.size() < spawnNumber) {
				Game::logger->Log("SGCannon", "Trying to spawn %i when spawns size is only %i", spawnNumber, activeSpawns.size());
				return;
			}
			const auto& toSpawn = activeSpawns.at(spawnNumber);
			const auto pathIndex = GeneralUtils::GenerateRandomNumber<float_t>(0, toSpawn.spawnPaths.size() - 1);
			const auto* path = Game::zoneManager->GetZone()->GetPath(toSpawn.spawnPaths.at(pathIndex));
			if (!path) {
				Game::logger->Log("SGCannon", "Path %s at index %i is null", toSpawn.spawnPaths.at(pathIndex).c_str(), pathIndex);
				return;
			}

			auto info = EntityInfo{};
			info.lot = toSpawn.lot;
			info.spawnerID = self->GetObjectID();
			info.pos = path->pathWaypoints.at(0).position;

			info.settings = {
				new LDFData<SGEnemy>(u"SpawnData", toSpawn),
				new LDFData<std::string>(u"custom_script_server", "scripts/ai/ACT/SG_TARGET.lua"),
				new LDFData<std::string>(u"custom_script_client", "scripts/client/ai/SG_TARGET_CLIENT.lua"),
				new LDFData<std::string>(u"attached_path", path->pathName),
				new LDFData<uint32_t>(u"attached_path_start", 0),
				new LDFData<std::u16string>(u"groupID", u"SGEnemy")
			};

			Game::logger->Log("SGCannon", "Spawning enemy %i on path %s", toSpawn.lot, path->pathName.c_str());

			auto* enemy = Game::entityManager->CreateEntity(info, nullptr, self);
			Game::entityManager->ConstructEntity(enemy);

			auto* movementAI = new MovementAIComponent(enemy, {});

			enemy->AddComponent(eReplicaComponentType::MOVEMENT_AI, movementAI);

			movementAI->SetMaxSpeed(toSpawn.initialSpeed);
			movementAI->SetCurrentSpeed(toSpawn.initialSpeed);
			movementAI->SetHaltDistance(0.0f);

			std::vector<NiPoint3> pathWaypoints;

			for (const auto& waypoint : path->pathWaypoints) {
				pathWaypoints.push_back(waypoint.position);
			}

			if (GeneralUtils::GenerateRandomNumber<float_t>(0, 1) < 0.5f) {
				std::reverse(pathWaypoints.begin(), pathWaypoints.end());
			}

			movementAI->SetPath(pathWaypoints);

			enemy->AddDieCallback([this, self, enemy, name]() {
				RegisterHit(self, enemy, name);
				});

			// Save the enemy and tell it to start pathing
			if (enemy != nullptr) {
				const_cast<std::vector<LWOOBJID>&>(self->GetVar<std::vector<LWOOBJID>>(SpawnedObjects)).push_back(enemy->GetObjectID());
				GameMessages::SendPlatformResync(enemy, UNASSIGNED_SYSTEM_ADDRESS);
			}
		}
	} else if (name == EndGameBufferTimer) {
		RecordPlayerScore(self);
		StopGame(self, false);
	}
}

void
SGCannon::OnActivityTimerUpdate(Entity* self, const std::string& name, float_t timeRemaining, float_t elapsedTime) {
	ActivityManager::OnActivityTimerUpdate(self, name, timeRemaining, elapsedTime);
}

void SGCannon::StartGame(Entity* self) {
	if (self->GetVar<bool>(GameStartedVariable)) return;
	self->SetNetworkVar<uint32_t>(TimeLimitVariable, self->GetVar<uint32_t>(TimeLimitVariable));
	self->SetNetworkVar<bool>(AudioStartIntroVariable, true);
	self->SetVar<LOT>(CurrentRewardVariable, LOT_NULL);

	auto rewardObjects = Game::entityManager->GetEntitiesInGroup(constants.rewardModelGroup);
	for (auto* reward : rewardObjects) {
		reward->OnFireEventServerSide(self, ModelToBuildEvent);
	}

	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
	if (player != nullptr) {
		GetLeaderboardData(self, player->GetObjectID(), GetActivityID(self), 1);
		Game::logger->Log("SGCannon", "Sending ActivityStart");
		GameMessages::SendActivityStart(self->GetObjectID(), player->GetSystemAddress());

		GameMessages::SendPlayFXEffect(self->GetObjectID(), -1, u"start", "");

		self->SetNetworkVar<bool>(ClearVariable, true);
		DoGameStartup(self);

		if (!self->GetVar<bool>(FirstTimeDoneVariable)) {
			TakeActivityCost(self, player->GetObjectID());
		}

		self->SetVar<bool>(FirstTimeDoneVariable, true);
	}

	SpawnNewModel(self);
}

void SGCannon::DoGameStartup(Entity* self) {
	ResetVars(self);
	self->SetVar<bool>(GameStartedVariable, true);
	self->SetNetworkVar<bool>(ClearVariable, true);
	self->SetVar<uint32_t>(ThisWaveVariable, 0);

	if (constants.firstWaveStartTime < 1) {
		constants.firstWaveStartTime = 1;
	}

	ActivityTimerStart(self, SpawnWaveTimer, constants.firstWaveStartTime,
		constants.firstWaveStartTime);
}

void SGCannon::SpawnNewModel(Entity* self) {

	// Add a new reward to the existing rewards
	const auto currentReward = self->GetVar<LOT>(CurrentRewardVariable);
	if (currentReward != -1) {
		auto rewards = self->GetVar<std::vector<LOT>>(RewardsVariable);
		rewards.push_back(currentReward);
		self->SetNetworkVar<int32_t>(RewardAddedVariable, currentReward);
	}

	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
	if (player != nullptr) {
		for (auto* rewardModel : Game::entityManager->GetEntitiesInGroup(constants.rewardModelGroup)) {
			uint32_t lootMatrix;
			switch (self->GetVar<uint32_t>(MatrixVariable)) {
			case 1:
				lootMatrix = constants.scoreLootMatrix1;
				break;
			case 2:
				lootMatrix = constants.scoreLootMatrix2;
				break;
			case 3:
				lootMatrix = constants.scoreLootMatrix3;
				break;
			case 4:
				lootMatrix = constants.scoreLootMatrix4;
				break;
			case 5:
				lootMatrix = constants.scoreLootMatrix5;
				break;
			default:
				lootMatrix = 0;
			}

			if (lootMatrix != 0) {
				std::unordered_map<LOT, int32_t> toDrop = {};
				toDrop = LootGenerator::Instance().RollLootMatrix(player, lootMatrix);

				for (auto drop : toDrop) {
					rewardModel->OnFireEventServerSide(self, ModelToBuildEvent, drop.first);
					self->SetVar<LOT>(CurrentRewardVariable, drop.first);
				}
			}
		}
	}
}

void SGCannon::RemovePlayer(LWOOBJID playerID) {
	auto* player = Game::entityManager->GetEntity(playerID);
	if (player == nullptr)
		return;

	auto* playerObject = dynamic_cast<Player*>(player);
	if (playerObject == nullptr)
		return;

	auto* character = playerObject->GetCharacter();
	if (character != nullptr) {
		playerObject->SendToZone(character->GetLastNonInstanceZoneID());
	}
}

void SGCannon::OnRequestActivityExit(Entity* self, LWOOBJID player, bool canceled) {
	if (canceled) {
		StopGame(self, canceled);
		RemovePlayer(player);
	}
}


void SGCannon::StartChargedCannon(Entity* self, uint32_t optionalTime) {
	optionalTime = optionalTime == 0 ? constants.chargedTime : optionalTime;
	self->SetVar<bool>(SuperChargePausedVariable, false);
	ToggleSuperCharge(self, true);
	ActivityTimerStart(self, SuperChargeTimer, 1, optionalTime);

	if (!self->GetVar<bool>(WaveStatusVariable)) {
		PauseChargeCannon(self);
	}
}

void SGCannon::TimerToggle(Entity* self, bool start) {
	if (start) {
		self->SetNetworkVar<uint32_t>(CountVariable, self->GetVar<uint32_t>(TimeLimitVariable));
		self->SetVar<bool>(GameStartedVariable, true);
	} else {
		self->SetNetworkVar<bool>(StopVariable, true);
	}
}

void SGCannon::SpawnObject(Entity* self, const SGEnemy& toSpawn, bool spawnNow) {
	auto activeSpawns = self->GetVar<std::vector<SGEnemy>>(ActiveSpawnsVariable);
	activeSpawns.push_back(toSpawn);
	self->SetVar<std::vector<SGEnemy>>(ActiveSpawnsVariable, activeSpawns);

	self->SetVar(SpawnNumberVariable, activeSpawns.size() - 1);
	const auto timerName = DoSpawnTimer + std::to_string(activeSpawns.size() - 1);

	if (spawnNow) {
		if (toSpawn.minSpawnTime > 0 && toSpawn.maxSpawnTime > 0) {
			const auto spawnTime = GeneralUtils::GenerateRandomNumber<float_t>(toSpawn.minSpawnTime, toSpawn.maxSpawnTime);

			ActivityTimerStart(self, timerName, spawnTime, spawnTime);
		} else {
			ActivityTimerStart(self, timerName, 1, 1);
		}
	} else if (toSpawn.respawns) {
		const auto spawnTime = GeneralUtils::GenerateRandomNumber<float_t>(toSpawn.minRespawnTime, toSpawn.maxRespawnTime);

		ActivityTimerStart(self, timerName, spawnTime, spawnTime);
	}
}

void SGCannon::RecordPlayerScore(Entity* self) {
	const auto totalScore = self->GetVar<uint32_t>(TotalScoreVariable);
	const auto currentWave = self->GetVar<uint32_t>(ThisWaveVariable);

	if (currentWave > 0) {
		auto totalWaveScore = 0;
		auto playerScores = self->GetVar<std::vector<int32_t>>(PlayerScoresVariable);

		for (const auto& waveScore : playerScores) {
			totalWaveScore += waveScore;
		}

		if (currentWave >= playerScores.size()) {
			playerScores.push_back(totalWaveScore);
		} else {
			playerScores[currentWave] = totalWaveScore;
		}
	}
}

void SGCannon::PlaySceneAnimation(Entity* self, const std::u16string& animationName, bool onCannon, bool onPlayer, float_t priority) {
	for (auto* cannon : Game::entityManager->GetEntitiesInGroup("cannongroup")) {
		RenderComponent::PlayAnimation(cannon, animationName, priority);
	}

	if (onCannon) {
		RenderComponent::PlayAnimation(self, animationName, priority);
	}

	if (onPlayer) {
		auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
		if (player != nullptr) {
			RenderComponent::PlayAnimation(player, animationName, priority);
		}
	}
}

void SGCannon::PauseChargeCannon(Entity* self) {
	const auto time = std::max((uint32_t)std::ceil(ActivityTimerGetCurrentTime(self, SuperChargeTimer)), (uint32_t)1);

	self->SetVar<bool>(SuperChargePausedVariable, true);
	self->SetVar<uint32_t>(CurrentSuperChargedTimeVariable, time);
	self->SetNetworkVar<uint32_t>(ChargeCountingVariable, time);

	ActivityTimerStop(self, SuperChargeTimer);
}

void SGCannon::StopGame(Entity* self, bool cancel) {
	self->SetNetworkVar<bool>(ReSetSuperChargeVariable, true);
	self->SetNetworkVar<bool>(HideSuperChargeVariable, true);

	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
	if (player == nullptr)
		return;

	ToggleSuperCharge(self, false);

	// The player won, store all the score and send rewards
	if (!cancel) {
		int32_t percentage = 0.0f;
		auto misses = self->GetVar<uint32_t>(MissesVariable);
		auto fired = self->GetVar<uint32_t>(ShotsFiredVariable);

		if (fired > 0) {
			percentage = misses / fired;
		}

		auto* missionComponent = player->GetComponent<MissionComponent>();

		if (missionComponent != nullptr) {
			missionComponent->Progress(eMissionTaskType::PERFORM_ACTIVITY, self->GetVar<uint32_t>(TotalScoreVariable), self->GetObjectID(), "performact_score");
			missionComponent->Progress(eMissionTaskType::PERFORM_ACTIVITY, self->GetVar<uint32_t>(MaxStreakVariable), self->GetObjectID(), "performact_streak");
			missionComponent->Progress(eMissionTaskType::ACTIVITY, m_CannonLot, 0, "", self->GetVar<uint32_t>(TotalScoreVariable));
		}

		LootGenerator::Instance().GiveActivityLoot(player, self, GetGameID(self), self->GetVar<uint32_t>(TotalScoreVariable));

		SaveScore(self, player->GetObjectID(),
			static_cast<float>(self->GetVar<uint32_t>(TotalScoreVariable)), static_cast<float>(self->GetVar<uint32_t>(MaxStreakVariable)), percentage);

		StopActivity(self, player->GetObjectID(), self->GetVar<uint32_t>(TotalScoreVariable), self->GetVar<uint32_t>(MaxStreakVariable), percentage);
		self->SetNetworkVar<bool>(AudioFinalWaveDoneVariable, true);

		// Give the player the model rewards they earned
		auto* inventory = player->GetComponent<InventoryComponent>();
		if (inventory != nullptr) {
			for (const auto rewardLot : self->GetVar<std::vector<LOT>>(RewardsVariable)) {
				inventory->AddItem(rewardLot, 1, eLootSourceType::ACTIVITY, eInventoryType::MODELS);
			}
		}

		self->SetNetworkVar<std::u16string>(u"UI_Rewards",
			GeneralUtils::to_u16string(self->GetVar<uint32_t>(TotalScoreVariable)) + u"_0_0_0_0_0_0"
		);
	}

	GameMessages::SendActivityStop(self->GetObjectID(), false, cancel, player->GetSystemAddress());
	self->SetVar<bool>(GameStartedVariable, false);
	ActivityTimerStopAllTimers(self);

	// Destroy all spawners
	for (auto* entity : Game::entityManager->GetEntitiesInGroup("SGEnemy")) {
		entity->Kill();
	}

	ResetVars(self);
}

void SGCannon::RegisterHit(Entity* self, Entity* target, const std::string& timerName) {
	const auto& spawnInfo = target->GetVar<SGEnemy>(u"SpawnData");

	if (spawnInfo.respawns) {
		const auto respawnTime = GeneralUtils::GenerateRandomNumber<float_t>(spawnInfo.minRespawnTime, spawnInfo.maxRespawnTime);

		ActivityTimerStart(self, timerName, respawnTime, respawnTime);
	}

	int score = spawnInfo.score;

	if (score > 0) {
		score += score * GetCurrentBonus(self);

		if (!self->GetVar<bool>(SuperChargeActiveVariable)) {
			self->SetVar<uint32_t>(u"m_curStreak", self->GetVar<uint32_t>(u"m_curStreak") + 1);
		}
	} else {
		if (!self->GetVar<bool>(SuperChargeActiveVariable)) {
			self->SetVar<uint32_t>(u"m_curStreak", 0);
		}

		self->SetNetworkVar<bool>(u"hitFriend", true);
	}

	auto lastSuperTotal = self->GetVar<uint32_t>(u"LastSuperTotal");

	auto scScore = self->GetVar<uint32_t>(TotalScoreVariable) - lastSuperTotal;

	Game::logger->Log("SGCannon", "LastSuperTotal: %i, scScore: %i, constants.chargedPoints: %i",
		lastSuperTotal, scScore, constants.chargedPoints
	);

	if (!self->GetVar<bool>(SuperChargeActiveVariable) && scScore >= constants.chargedPoints && score >= 0) {
		StartChargedCannon(self);
		self->SetNetworkVar<float>(u"SuperChargeBar", 100.0f);
		self->SetVar<uint32_t>(u"LastSuperTotal", self->GetVar<uint32_t>(TotalScoreVariable));
	}

	UpdateStreak(self);

	GameMessages::SendNotifyClientShootingGalleryScore(self->GetObjectID(), UNASSIGNED_SYSTEM_ADDRESS,
		0.0f,
		score,
		target->GetObjectID(),
		target->GetPosition()
	);

	auto newScore = (int)self->GetVar<uint32_t>(TotalScoreVariable) + score;

	if (newScore < 0) {
		newScore = 0;
	}

	self->SetVar<uint32_t>(TotalScoreVariable, newScore);

	self->SetNetworkVar<uint32_t>(u"updateScore", newScore);

	self->SetNetworkVar<std::u16string>(u"beatHighScore", GeneralUtils::to_u16string(newScore));

	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));
	if (player == nullptr) return;

	auto missionComponent = player->GetComponent<MissionComponent>();
	if (missionComponent == nullptr) return;

	missionComponent->Progress(eMissionTaskType::SMASH, spawnInfo.lot, self->GetObjectID());
}

void SGCannon::UpdateStreak(Entity* self) {
	const auto streakBonus = GetCurrentBonus(self);

	const auto curStreak = self->GetVar<uint32_t>(u"m_curStreak");

	const auto marks = curStreak % 3;

	self->SetNetworkVar<uint32_t>(u"cStreak", curStreak);

	if (curStreak >= 0 && curStreak < 13) {
		if (marks == 1) {
			self->SetNetworkVar<bool>(u"Mark1", true);
		} else if (marks == 2) {
			self->SetNetworkVar<bool>(u"Mark2", true);
		} else if (marks == 0 && curStreak > 0) {
			self->SetVar<float_t>(u"StreakBonus", streakBonus);
			self->SetNetworkVar<bool>(u"ShowStreak", streakBonus + 1);
			self->SetNetworkVar<bool>(u"Mark3", true);
		} else {
			self->SetVar<float_t>(u"StreakBonus", streakBonus);
			self->SetNetworkVar<bool>(u"UnMarkAll", true);
		}
	}
	auto maxStreak = self->GetVar<uint32_t>(MaxStreakVariable);
	if (maxStreak < curStreak) self->SetVar<uint32_t>(MaxStreakVariable, curStreak);
}

float_t SGCannon::GetCurrentBonus(Entity* self) {
	auto streak = self->GetVar<uint32_t>(u"m_curStreak");

	if (streak > 12) {
		streak = 12;
	}

	return streak / 3;
}

void SGCannon::ToggleSuperCharge(Entity* self, bool enable) {
	if (enable && self->GetVar<bool>(SuperChargeActiveVariable))
		return;

	auto* player = Game::entityManager->GetEntity(self->GetVar<LWOOBJID>(PlayerIDVariable));

	if (player == nullptr) {
		Game::logger->Log("SGCannon", "Player not found in toggle super charge");
		return;
	}

	auto* inventoryComponent = player->GetComponent<InventoryComponent>();

	auto equippedItems = inventoryComponent->GetEquippedItems();

	Game::logger->Log("SGCannon", "Player has %d equipped items", equippedItems.size());

	auto skillID = constants.cannonSkill;
	auto cooldown = constants.cannonRefireRate;

	auto* selfInventoryComponent = self->GetComponent<InventoryComponent>();

	if (inventoryComponent == nullptr) {
		Game::logger->Log("SGCannon", "Inventory component not found");
		return;
	}

	if (enable) {
		Game::logger->Log("SGCannon", "Player is activating super charge");
		selfInventoryComponent->UpdateSlot("greeble_r", { ObjectIDManager::GenerateRandomObjectID(), 6505, 1, 0 });
		selfInventoryComponent->UpdateSlot("greeble_l", { ObjectIDManager::GenerateRandomObjectID(), 6506, 1, 0 });

		// TODO: Equip items
		skillID = constants.cannonSuperChargeSkill;
		cooldown = 400;
	} else {
		selfInventoryComponent->UpdateSlot("greeble_r", { ObjectIDManager::GenerateRandomObjectID(), 0, 0, 0 });
		selfInventoryComponent->UpdateSlot("greeble_l", { ObjectIDManager::GenerateRandomObjectID(), 0, 0, 0 });

		self->SetNetworkVar<float>(u"SuperChargeBar", 0);

		Game::logger->Log("SGCannon", "Player disables super charge");

		// TODO: Unequip items
		for (const auto& equipped : equippedItems) {
			if (equipped.first == "special_r" || equipped.first == "special_l") {
				Game::logger->Log("SGCannon", "Trying to unequip a weapon, %i", equipped.second.lot);

				auto* item = inventoryComponent->FindItemById(equipped.second.id);

				if (item != nullptr) {
					inventoryComponent->UnEquipItem(item);
				} else {
					Game::logger->Log("SGCannon", "Item not found, %i", equipped.second.lot);
				}
			}
		}
		cooldown = 800;
		self->SetVar<uint32_t>(NumberOfChargesVariable, 0);
	}

	const auto& constants = GetConstants();

	auto* shootingGalleryComponent = self->GetComponent<ShootingGalleryComponent>();

	if (shootingGalleryComponent == nullptr) {
		return;
	}

	DynamicShootingGalleryParams properties = shootingGalleryComponent->GetDynamicParams();

	properties.cannonFOV = 58.6f;
	properties.cannonVelocity = 129.0;
	properties.cannonRefireRate = cooldown;
	properties.cannonMinDistance = 30;
	properties.cannonTimeout = -1;

	shootingGalleryComponent->SetDynamicParams(properties);

	Game::entityManager->SerializeEntity(self);
	Game::entityManager->SerializeEntity(player);

	self->SetNetworkVar<uint64_t>(CannonBallSkillIDVariable, skillID);
	self->SetVar<bool>(SuperChargeActiveVariable, enable);
}

std::vector<std::vector<SGEnemy>> SGCannon::GetWaves() {
	return
	std::vector<std::vector<SGEnemy>>{
		std::vector<SGEnemy>{
			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_1", "Wave_1_Ship_3"},
			.lot = 6015,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_2", "Wave_1_Ship_4"},
			.lot = 6300,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Sub_1", "Wave_1_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 10.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Sub_1", "Wave_2_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_FShip_1", "Wave_3_FShip_2"},
			.lot = 2168,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 5.0,
			.respawns = true,
			.minRespawnTime = 2.0,
			.maxRespawnTime = 5.0,
			.initialSpeed = 1.0,
			.score = -1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			}
		},

		std::vector<SGEnemy>{
			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_1", "Wave_1_Ship_3"},
			.lot = 6015,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_2", "Wave_1_Ship_4"},
			.lot = 6300,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Ship_1"},
			.lot = 6300,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Ship_2"},
			.lot = 6015,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Sub_1", "Wave_1_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Sub_1", "Wave_2_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Duck_1", "Wave_1_Duck_2"},
			.lot = 5946,
			.minSpawnTime = 5.0,
			.maxSpawnTime = 10.0,
			.respawns = true,
			.minRespawnTime = 5.0,
			.maxRespawnTime = 10.0,
			.initialSpeed = 4.0,
			.score = 5000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_FShip_1", "Wave_3_FShip_2"},
			.lot = 2168,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 5.0,
			.respawns = true,
			.minRespawnTime = 2.0,
			.maxRespawnTime = 5.0,
			.initialSpeed = 1.0,
			.score = -1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			}
		},

		std::vector<SGEnemy>{

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_1", "Wave_1_Ship_3"},
			.lot = 6015,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ship_2", "Wave_1_Ship_4"},
			.lot = 6300,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Ship_1", "Wave_2_Ship_2"},
			.lot = 6015,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_Ship_1", "Wave_3_Ship_2"},
			.lot = 6300,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1500,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Sub_1", "Wave_1_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_2_Sub_1", "Wave_2_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_Sub_1", "Wave_3_Sub_2"},
			.lot = 6016,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 2.0,
			.respawns = true,
			.minRespawnTime = 0.0,
			.maxRespawnTime = 2.0,
			.initialSpeed = 2.0,
			.score = 1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Duck_1", "Wave_1_Duck_2"},
			.lot = 5946,
			.minSpawnTime = 5.0,
			.maxSpawnTime = 10.0,
			.respawns = true,
			.minRespawnTime = 5.0,
			.maxRespawnTime = 10.0,
			.initialSpeed = 4.0,
			.score = 5000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_1_Ness_1", "Wave_1_Ness_2", "Wave_2_Ness_1"},
			.lot = 2565,
			.minSpawnTime = 10.0,
			.maxSpawnTime = 15.0,
			.respawns = true,
			.minRespawnTime = 10.0,
			.maxRespawnTime = 15.0,
			.initialSpeed = 2.0,
			.score = 10000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = true,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_FShip_1", "Wave_3_FShip_2"},
			.lot = 2168,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 5.0,
			.respawns = true,
			.minRespawnTime = 2.0,
			.maxRespawnTime = 5.0,
			.initialSpeed = 1.0,
			.score = -1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			},

			SGEnemy{
			.spawnPaths = std::vector<std::string>{"Wave_3_FShip_1", "Wave_3_FShip_2"},
			.lot = 2168,
			.minSpawnTime = 0.0,
			.maxSpawnTime = 5.0,
			.respawns = true,
			.minRespawnTime = 2.0,
			.maxRespawnTime = 5.0,
			.initialSpeed = 1.0,
			.score = -1000,
			.changeSpeedAtWaypoint = false,
			.speedChangeChance = 0.0,
			.minSpeed = 1.0,
			.maxSpeed = 1.0,
			.isMovingPlatform = false,
			.despawnOnLastWaypoint = true
			}
		}
	};
}

void SGCannon::ResetVars(Entity* self) {
	self->SetVar<uint32_t>(SpawnNumberVariable, 0);
	self->SetVar<uint32_t>(CurrentSpawnNumberVariable, 0);
	self->SetVar<uint32_t>(ThisWaveVariable, 0);
	self->SetVar<uint32_t>(GameScoreVariable, 0);
	self->SetVar<uint32_t>(GameTimeVariable, 0);
	self->SetVar<bool>(GameStartedVariable, false);
	self->SetVar<uint32_t>(ShotsFiredVariable, 0);
	self->SetVar<uint32_t>(MaxStreakVariable, 0);
	self->SetVar<uint32_t>(MissesVariable, 0);
	self->SetVar<uint32_t>(CurrentStreakVariable, 0);
	self->SetVar<uint32_t>(CurrentSuperChargedTimeVariable, 0);
	self->SetVar<std::vector<uint32_t>>(StreakBonusVariable, {});
	self->SetVar<uint32_t>(LastSuperTotalVariable, 0);
	self->SetVar<LOT>(CurrentRewardVariable, LOT_NULL);
	self->SetVar<std::vector<LOT>>(RewardsVariable, {});
	self->SetVar<uint32_t>(TotalScoreVariable, 0);

	const_cast<std::vector<SGEnemy>&>(self->GetVar<std::vector<SGEnemy>>(ActiveSpawnsVariable)).clear();
	self->SetVar<std::vector<SGEnemy>>(ActiveSpawnsVariable, {});

	const_cast<std::vector<LWOOBJID>&>(self->GetVar<std::vector<LWOOBJID>>(SpawnedObjects)).clear();
	self->SetVar<std::vector<LWOOBJID>>(SpawnedObjects, {});

	if (self->GetVar<bool>(InitVariable)) {
		ToggleSuperCharge(self, false);
	}

	self->SetVar<uint32_t>(ImpactSkillVariale, constants.impactSkillID);
	self->SetVar<std::vector<int32_t>>(PlayerScoresVariable, {});
	ActivityTimerStopAllTimers(self);
}

SGConstants SGCannon::GetConstants() {
	return SGConstants{
		.playerStartPosition = Vector3 { -908.542480, 229.773178, -908.542480 },
		.playerStartRotation = Quaternion { 0.91913521289825, 0, 0.39394217729568, 0 },
		.cannonLot = 1864,
		.impactSkillID = 34,
		.projectileLot = 1822,
		.playerOffset = Vector3 { 6.652, -2, 1.5 },
		.rewardModelMatrix = 157,
		.cannonVelocity = 129.0,
		.cannonMinDistance = 30.0,
		.cannonRefireRate = 800.0,
		.cannonBarrelOffset = Vector3 { 0, 4.3, 9 },
		.cannonSuperchargedProjectileLot = 6297,
		.cannonProjectileLot = 1822,
		.cannonSuperChargeSkill = 249,
		.cannonSkill = 228,
		.cannonTimeout = -1,
		.cannonFOV = 58.6,
		.useLeaderboards = true,
		.streakModifier = 2,
		.chargedTime = 10,
		.chargedPoints = 25000,
		.rewardModelGroup = "QBRewardGroup",
		.activityID = 1864,
		.scoreReward1 = 50000,
		.scoreLootMatrix1 = 157,
		.scoreReward2 = 100000,
		.scoreLootMatrix2 = 187,
		.scoreReward3 = 200000,
		.scoreLootMatrix3 = 188,
		.scoreReward4 = 400000,
		.scoreLootMatrix4 = 189,
		.scoreReward5 = 800000,
		.scoreLootMatrix5 = 190,
		.firstWaveStartTime = 4.0,
		.inBetweenWavePause = 7.0
	};
}
