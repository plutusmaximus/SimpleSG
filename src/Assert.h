#pragma once

#if defined(_MSC_VER)

//
// Enable/disable the assert dialog.
// Returns the prior value
//
bool SetAssertDialogEnabled(const bool enabled);

#ifndef NDEBUG

bool ShowAssertDialog(const char* expression, const char* fileName, const int lineNum);

#define Assert(expression)(void) ((!!(expression)) || (ShowAssertDialog(#expression, __FILE__, __LINE__) ? __debugbreak(), false : false))
// verify is like assert excpet that it can be used in boolean expressions.
// 
// For ex.
// 
// if(Verify(nullptr != p))...
// 
// Or
// 
// return Verify(x > y) ? x : -1;
// 
// !! is used to ensure that any overloaded operators used to evaluate expr
// do not end up at &&.
#define Verify(expression) ((!!(expression)) || (ShowAssertDialog(#expression, __FILE__, __LINE__) ? __debugbreak(), false : false))

#else	//NDEBUG

#define	Verify(expr) (!!(expr))

#endif	//NDEBUG

#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER