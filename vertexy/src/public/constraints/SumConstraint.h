// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ConstraintOperator.h"
#include "IConstraint.h"

namespace Vertexy
{

	/**
	 * Given three variables, constrains their values such that Sum = Term1 + Term2
	 */
	class SumConstraint : public IConstraint
	{
	public:
		SumConstraint(const ConstraintFactoryParams& params, VarID inSum, VarID inTerm1, VarID inTerm2);

		struct SumConstraintFactory
		{
			static SumConstraint* construct(const ConstraintFactoryParams& params, VarID inSum, VarID inTerm1, VarID inTerm2);
		};
		typedef SumConstraintFactory Factory;

		virtual EConstraintType getConstraintType() const override { return EConstraintType::Sum; }
		virtual vector<VarID> getConstrainingVariables() const override;
		virtual bool initialize(IVariableDatabase* db) override;
		virtual void reset(IVariableDatabase* db) override;
		virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& bRemoveWatch) override;
		virtual bool checkConflicting(IVariableDatabase* db) const override;

	protected:
		/**
		* Combines two ValueSets by either adding or subtracting them
		*
		* @param db The variable database used
		* @param destSize The domain size of the returned ValueSet
		* @param one, two The variables whose ValueSets are combined; in the case of subtraction, two is subtracted from one
		* @param addSets Should be true if adding the sets and false if subtracting the sets
		* @return The combined ValueSet, either the sum or difference of one and two's ValueSets
		*/
		ValueSet combineValueSets(IVariableDatabase* db, int destSize, VarID one, VarID two, bool addSets) const;

		// The minimum domain value among variables in the constraint, used as an offset
		int m_minVal;

		VarID m_sum;
		VarID m_term1;
		VarID m_term2;
		WatcherHandle m_sumWatch = INVALID_WATCHER_HANDLE;
		WatcherHandle m_term1Watch = INVALID_WATCHER_HANDLE;
		WatcherHandle m_term2Watch = INVALID_WATCHER_HANDLE;
	};

} // namespace Vertexy