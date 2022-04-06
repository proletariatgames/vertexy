// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "IRestartPolicy.h"
#include "ds/AveragingBoundedQueue.h"

namespace csolver
{
/** Restart policy based on LBD (literal-block-distance) quality, inspired by Glucose SAT solver
 *  See http://www.pragmaticsofsat.org/2012/slides-glucose.pdf for more detail
 */
class LBDRestartPolicy : public IRestartPolicy
{
public:
	LBDRestartPolicy(const ConstraintSolver& solver);

	virtual bool shouldRestart() override;
	virtual void onClauseLearned(const ClauseConstraint& learnedClause) override;
	virtual void onRestarted() override;

protected:
	/** Queue for determining average size of assignment stack */
	TAveragingBoundedQueue<uint32_t> m_assignmentStackSizeQueue;
	/** Queue for determining average LBD of learned clauses */
	TAveragingBoundedQueue<uint32_t> m_lbdSizeQueue;
	/** Total sum of LBD of every learned clause since last restart */
	uint32_t m_lbdTotal = 0;
	/** Total number of learned clauses since last restart */
	uint32_t m_conflictCounter = 0;
};

} // namespace csolver