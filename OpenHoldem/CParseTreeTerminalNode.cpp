//*******************************************************************************
//
// This file is part of the OpenHoldem project
//   Download page:         http://code.google.com/p/openholdembot/
//   Forums:                http://www.maxinmontreal.com/forums/index.php
//   Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//*******************************************************************************
//
// Purpose:
//
//*******************************************************************************

#include "stdafx.h" 
#include "CParseTreeTerminalNode.h"

#include <math.h>
#include "CAutoplayerTrace.h"
#include "CEngineContainer.h"
#include "CFunctionCollection.h"
#include "CParserSymbolTable.h"
#include "CPreferences.h"
#include "CSymbolEngineChipAmounts.h"
#include "CSymbolEngineMemorySymbols.h"
#include "CSymbolEngineOpenPPLUserVariables.h"
#include "CSymbolEngineTableLimits.h"
#include "FloatingPoint_Comparisions.h"
#include "NumericalFunctions.h"
#include "OH_MessageBox.h"
#include "StringFunctions.h"
#include "TokenizerConstants.h"

CParseTreeTerminalNode::CParseTreeTerminalNode(int relative_line_number) : 
    CParseTreeNode(relative_line_number) {
  _terminal_name   = "";
  _constant_value  = 0.0;
}

CParseTreeTerminalNode::~CParseTreeTerminalNode() {
}

void CParseTreeTerminalNode::MakeConstant(double value) {
	_node_type = kTokenNumber;
	_constant_value = value;
}

void CParseTreeTerminalNode::MakeIdentifier(CString name) {
	_node_type = kTokenIdentifier;
  assert(name != "");
	_terminal_name = name;
  assert(p_parser_symbol_table != NULL);
  p_parser_symbol_table->VerifySymbol(name);
}

// !!!!! Rename to fixed action
void CParseTreeTerminalNode::MakeAction(int action_constant) {
	assert(TokenIsOpenPPLAction(action_constant));
  CString action_name = TokenString(action_constant);
  assert(action_name != "");
  MakeIdentifier(action_name);
}

void CParseTreeTerminalNode::MakeUserVariableDefinition(CString uservariable) {
  assert((uservariable.Left(4).MakeLower() == "user")
    || (uservariable.Left(3) == "me_"));
	_node_type = kTokenActionUserVariableToBeSet;
	_terminal_name = uservariable;
}

double CParseTreeTerminalNode::Evaluate(bool log /* = false */){
  write_log(preferences.debug_formula(), 
    "[CParseTreeTerminalNode] Evaluating node type %i %s\n", 
		_node_type, TokenString(_node_type));
  p_autoplayer_trace->SetLastEvaluatedRelativeLineNumber(_relative_line_number);
	// Most common types first: numbers and identifiers
	if (_node_type == kTokenNumber)	{
		write_log(preferences.debug_formula(), 
      "[CParseTreeTerminalNode] Number evaluates to %6.3f\n",
			_constant_value);
		return _constant_value;
	}	else if (_node_type == kTokenIdentifier) {
    assert(_first_sibbling  == NULL);
    assert(_second_sibbling == NULL);
    assert(_third_sibbling  == NULL);
		assert(_terminal_name != "");
		double value = EvaluateIdentifier(_terminal_name, log);
		write_log(preferences.debug_formula(), 
      "[CParseTreeTerminalNode] Identifier evaluates to %6.3f\n", value);
    // In case of f$-functions the line changed inbetween,
    // so we have to set it to the current location (again)
    // for the next log.
    p_autoplayer_trace->SetLastEvaluatedRelativeLineNumber(_relative_line_number);
		return value;
	}
	// Actions second, which are also "unary".
	// We have to encode all possible outcomes in a single floating-point,
	// therefore:
	// * positive values mean: raise size (by big-blinds, raise-to-semantics) 
	// * negative values mean: elementary actions
  else if (_node_type == kTokenActionUserVariableToBeSet) {
    // User-variables are a special case of elementary actions
    // Therefore need to be handled first.
    SetUserVariable(_terminal_name);
		return kUndefinedZero;
  } else if (TokenIsElementaryAction(_node_type)) {
		return (0 - _node_type);
  }
	// This must not happen for a terminal node
	assert(false);
	return kUndefined;
}

CString CParseTreeTerminalNode::EvaluateToString(bool log /* = false */) {
  double numerical_result = Evaluate(log);
  CString result;
  if (IsInteger(numerical_result) && EvaluatesToBinaryNumber()) {
    // Generqate binary representation;
    result.Format("%s", IntToBinaryString(int(numerical_result)));
  } else {
    // Generate floating-point representation
    // with 3 digits precision
    result.Format("%.3f", numerical_result);
  }
  return result;
}

double CParseTreeTerminalNode::EvaluateIdentifier(CString name, bool log) {
	// EvaluateSymbol cares about ALL symbols, 
	// including DLL and PokerTracker.
	double result;
	p_engine_container->EvaluateSymbol(name, &result, log);
	return result;
}

CString CParseTreeTerminalNode::Serialize() {
  if (_node_type == kTokenIdentifier) {
    return _terminal_name;
  } else if (_node_type == kTokenNumber) {
    return Number2CString(_constant_value);
  } else {
    // Unhandled note-type, probably new and therefore not yet handled
    write_log(k_always_log_errors, "[CParseTreeTerminalNode] ERROR: Unhandled node-tzpe %i in serialiyation of parse-tree\n",
      _node_type);
    return "";
  }
}

bool CParseTreeTerminalNode::IsBinaryIdentifier() {
  const int kNumberOfElementaryBinaryIdentifiers = 21;
  static const char* kElementaryBinaryIdentifiers[kNumberOfElementaryBinaryIdentifiers] = {
    "pcbits",              "playersactivebits",  "playersdealtbits",
    "playersplayingbits",  "playersblindbits",   "opponentsseatedbits",
    "opponentsactivebits", "opponentsdealtbits", "opponentsplayingbits",
    "opponentsblindbits",  "flabitgs",           "rankbits",
    "rankbitscommon",      "rankbitsplayer",     "rankbitspoker",
    "srankbits",           "srankbitscommon",    "srankbitsplayer",
    "srankbitspoker",      "myturnbits",         "pcbits"};
  const int kNumberOfParameterizedBinaryIdentifiers = 4;
  static const char* kParameterizedBinaryIdentifiers[kNumberOfParameterizedBinaryIdentifiers] = {
    "chairbit$", "raisbits", "callbits", "foldbits"};

  if (_node_type != kTokenIdentifier) return false;
  assert(_terminal_name != "");
  // Check elementary binary identifiers first
  for (int i=0; i<kNumberOfElementaryBinaryIdentifiers; ++i) {
    if (_terminal_name == kElementaryBinaryIdentifiers[i]) return true;
  }
  // Then check parameterized binary symbols
  for (int i=0; i<kNumberOfParameterizedBinaryIdentifiers; ++i) {
    if (StringAIsPrefixOfStringB(kParameterizedBinaryIdentifiers[i],
        _terminal_name)) {
      return true;
    }                                 
  }
  // Not a binary identifier
  return false;
}

void CParseTreeTerminalNode::SetUserVariable(CString name) {
  if (name.Left(4).MakeLower() == "user") {   
    p_symbol_engine_openppl_user_variables->Set(name);
  } else if (name.Left(3) == "me_") {
    double temp_result;
    p_symbol_engine_memory_symbols->EvaluateSymbol(name, 
      &temp_result, true);
  }
  else {
    assert(kThisMustNotHappen);
  }
}

bool CParseTreeTerminalNode::EvaluatesToBinaryNumber() {
  if (TokenEvaluatesToBinaryNumber(_node_type)) {
    // Operation that evaluates to binary number,
    // e.g. bit-shift, logical and, etc.
    return true;
  }
  else if (IsBinaryIdentifier()) return true;
  else if ((_node_type == kTokenIdentifier)
      && p_function_collection->EvaluatesToBinaryNumber(_terminal_name)) {
    return true;
  }
  // Nothing binary
  return false;
}
