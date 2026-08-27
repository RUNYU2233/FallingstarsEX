#pragma once

// Register/stack access macros for Syringe hooks.
// These mirror the GET/GET_STACK family used by Phobos' Utilities/Macro.h,
// but are defined here so FallingStars does not depend on any external copy.
// They operate on the REGISTERS* pointer (R) passed to every DEFINE_HOOK.

#include <Syringe.h>

// Read a register as a value.
//   GET(TechnoClass*, pThis, ECX)
#define GET(type, var, reg) type var = R->reg<type>()

// Read a value from the stack at ESP + offset (offset relative to hook entry).
//   GET_STACK(int, damage, 0x4)
#define GET_STACK(type, var, offset) type var = R->Stack<type>(offset)

// Pointer to a stack slot (address of the value on the stack).
//   LEA_STACK(CoordStruct*, pCoord, 0xC)
#define LEA_STACK(type, var, offset) type var = R->lea_Stack<type>(offset)

// Reference to a stack slot (mutable).
//   REF_STACK(CoordStruct, coord, 0xC)
#define REF_STACK(type, var, offset) type& var = R->ref_Stack<type>(offset)

// Read a value from the EBP-relative frame (rarely useful; see skill notes).
//   GET_BASE(int, arg, 0x8)
#define GET_BASE(type, var, offset) type var = R->Base<type>(offset)

// Arithmetic helper for stack offsets documented at a different point.
#define STACK_OFFSET(current, wanted) ((current) - (wanted))

// Write a register back.
//   R->EAX(0x1234);
// (R->reg(value) overloads already exist on REGISTERS for this.)

// Common return shortcuts inside a DEFINE_HOOK body.
//   return 0;            // run stolen bytes, then continue at address + size
//   return 0x6FD0B6;     // jump to an explicit address, skip stolen bytes
//   return R->Origin() + 0xF;  // relative jump (use in DEFINE_HOOK_AGAIN)
