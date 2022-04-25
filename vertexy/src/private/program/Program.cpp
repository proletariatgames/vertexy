// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/Program.h"

using namespace Vertexy;

ProgramInstance* Program::s_currentInstance = nullptr;
int Program::s_nextFormulaUID = 1;
int Program::s_nextParameterUID = 1;

ProgramParameter::ProgramParameter()
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot define a ProgramParameter outside of a Program::define block!");
    m_uid = Program::allocateParameterUID();
}
