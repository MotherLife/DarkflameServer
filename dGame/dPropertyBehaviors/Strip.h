#ifndef __STRIP__H__
#define __STRIP__H__

#include "Action.h"
#include "StripUiPosition.h"

#include <vector>

class AMFArrayValue;

class Strip {
public:
	template<typename Msg>
	void HandleMsg(Msg& msg);

	void SendBehaviorBlocksToClient(AMFArrayValue& args) const;
	bool IsEmpty() const { return m_Actions.empty(); }
private:
	std::vector<Action> m_Actions;
	StripUiPosition m_Position;
};

#endif  //!__STRIP__H__
