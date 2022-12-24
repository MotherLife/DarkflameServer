#ifndef BUFFCOMPONENT_H
#define BUFFCOMPONENT_H

#include "dCommonVars.h"
#include "RakNetTypes.h"
#include <vector>
#include <unordered_map>
#include <map>
#include "Component.h"

class Entity;

/**
 * Extra information on effects to apply after applying a buff, for example whether to buff armor, imag or health and by how much
 */
struct BuffParameter
{
	int32_t buffId;
	std::string name;
	float value;
	std::vector<float> values;
	int32_t effectId;
};

/**
 * Meta information about a buff that can be applied, e.g. how long it's applied, who applied it, etc.
 */
struct Buff
{
	int32_t id = 0;
	float duration = 0;
	float time = 0;
	float tick = 0;
	float tickTime = 0;
	int32_t stacks = 0;
	LWOOBJID source = 0;
	int32_t behaviorID = 0;
	bool addImmunity = false;
	bool applyOnTeammates = false;
	bool cancelOnDamaged = false;
	bool cancelOnDeath = false;
	bool cancelOnLogout = false;
	bool cancelOnRemoveBuff = false;
	bool cancelOnDamageAbsDone = false;
	bool cancelOnUi = false;
	bool cancelOnUnequip = false;
	bool cancelOnZone = false;
	bool useRefCount = false;

	uint32_t refcount = 0;

};

/**
 * Allows for the application of buffs to the parent entity, altering health, armor and imagination.
 */
class BuffComponent : public Component {
public:
	static const uint32_t ComponentType = COMPONENT_TYPE_BUFF;

	explicit BuffComponent(Entity* parent);

	~BuffComponent();

	Entity* GetParent() const;

	void LoadFromXml(tinyxml2::XMLDocument* doc) override;

	void UpdateXml(tinyxml2::XMLDocument* doc) override;

	void Serialize(RakNet::BitStream* outBitStream, bool bIsInitialUpdate, unsigned int& flags);

	void Update(float deltaTime) override;

	/**
	 * Applies a buff to the parent entity
	 * @param buff the Buff to apply
	 */
	void ApplyBuff(Buff buff);

	/**
	 * Removes a buff from the parent entity, reversing its effects
	 * @param id the id of the buff to remove
	 * @param removeImmunity whether or not to remove immunity on removing the buff
	 */
	void RemoveBuff(int32_t id, bool fromUnEquip = false, bool removeImmunity = false);

	/**
	 * Returns whether or not the entity has a buff identified by `id`
	 * @param id the id of the buff to find
	 * @return whether or not the entity has a buff with the specified id active
	 */
	bool HasBuff(int32_t id);

	/**
	 * Returns whether or not the entity has a buff immunity identified by `id`
	 * @param id the id of the buff immunity to find
	 * @return whether or not the entity has a buff with the specified id active
	 */
	bool HasBuffImmunity(int32_t id);


	/**
	 * Applies the effects of the buffs on the entity, e.g.: changing armor, health, imag, etc.
	 * @param id the id of the buff effects to apply
	 */
	void ApplyBuffEffect(int32_t id);

	/**
	 * Reverses the effects of the applied buff
	 * @param id the id of the buff for which to remove the effects
	 */
	void RemoveBuffEffect(int32_t id);

	/**
	 * Removes all buffs for the entity and reverses all of their effects
	 */
	void RemoveAllBuffs();

	/**
	 * Removes all buffs for the entity and reverses all of their effects
	 */
	void Reset();

	/**
	 * Applies all effects for all buffs, active or not, again
	 */
	void ReApplyBuffs();

	/**
	 * Gets all the parameters (= effects), for the buffs that belong to this component
	 * @param buffId
	 * @return
	 */
	const std::vector<BuffParameter>& GetBuffParameters(int32_t buffId);

private:
	/**
	 * The currently active buffs
	 */
	std::map<int32_t, Buff> m_Buffs;

	/**
	 * The currently active buff immunities
	 */
	std::map<int32_t, Buff> m_BuffImmunities;

	/**
	 * Parameters (=effects) for each buff
	 */
	static std::unordered_map<int32_t, std::vector<BuffParameter>> m_Cache;
};

#endif // BUFFCOMPONENT_H
