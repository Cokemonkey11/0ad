/*
GUI
by Gustav Larsson
gee@pyro.nu
*/

//#include "stdafx.h"
#include "GUI.h"

using namespace std;


//--------------------------------------------------------
//  Error definitions
//--------------------------------------------------------
///DEFINE_ERROR(PS_FAIL,				"fail")
///DEFINE_ERROR(PS_OK,					"ok")
DEFINE_ERROR(PS_NAME_TAKEN,			"Reference name is taken")
DEFINE_ERROR(PS_OBJECT_FAIL,		"Object provided is null")
DEFINE_ERROR(PS_SETTING_FAIL,		"Setting does not exist")
DEFINE_ERROR(PS_VALUE_INVALID,		"Value provided is syntactically incorrect")
DEFINE_ERROR(PS_NEEDS_PGUI,			"m_pGUI is NULL when needed for a requested operation")
DEFINE_ERROR(PS_NAME_AMBIGUITY,		"Two or more objects are sharing name")
DEFINE_ERROR(PS_NEEDS_NAME,			"An object are trying to fit into a GUI without a name")

DEFINE_ERROR(PS_LEXICAL_FAIL,		"PS_LEXICAL_FAIL")
DEFINE_ERROR(PS_SYNTACTICAL_FAIL,	"PS_SYNTACTICAL_FAIL")
