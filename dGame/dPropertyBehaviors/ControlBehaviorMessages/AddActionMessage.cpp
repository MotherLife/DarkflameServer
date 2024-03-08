#include "AddActionMessage.h"

AddActionMessage::AddActionMessage(const AMFArrayValue& arguments)
	: BehaviorMessageBase{ arguments }
	, m_ActionIndex{ GetActionIndexFromArgument(arguments) }
	, m_ActionContext{ arguments } {

	const auto* const actionValue = arguments.GetArray("action");
	if (!actionValue) return;

	m_Action = Action{ *actionValue };

	Log::Debug("actionIndex {:d} stripId {:d} stateId {:d} type {:s} valueParameterName {:s} valueParameterString {:s} valueParameterDouble {:f} m_BehaviorId {:d}", m_ActionIndex, m_ActionContext.GetStripId(), GeneralUtils::ToUnderlying(m_ActionContext.GetStateId()), m_Action.GetType(), m_Action.GetValueParameterName(), m_Action.GetValueParameterString(), m_Action.GetValueParameterDouble(), m_BehaviorId);
}
