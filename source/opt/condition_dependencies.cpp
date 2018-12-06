
#include "source/opt/condition_dependencies.h"

#include <iostream>

#include "source/opt/function.h"

namespace spvtools {
namespace opt {

Pass::Status ConditionDependencies::Process() {
  for (auto& function : *get_module()) {
    std::cout << "Function %" << function.result_id();
    for (auto& entry : get_module()->entry_points()) {
      if (entry.GetSingleWordInOperand(1u) == function.result_id())
        std::cout << " "
                  << reinterpret_cast<const char*>(
                         entry.GetInOperand(2u).words.data());
    }
    std::cout << "\n";

    for (auto& block : function) {
      auto terminator = block.terminator();
      if (terminator->opcode() != SpvOpBranchConditional) continue;

      std::cout << " "
                << terminator->PrettyPrint(
                       SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES)
                << "\n  ";
      PrintCondition(terminator->GetSingleWordInOperand(0u));
      std::cout << "\n";
    }

    std::cout << "\n";
  }

  return Pass::Status::SuccessWithoutChange;
}

void ConditionDependencies::PrintCondition(uint32_t cond_id) {
  auto cond = get_def_use_mgr()->GetDef(cond_id);
  switch (cond->opcode()) {
    // Unary ops
    case SpvOpLogicalNot:
      std::cout << "!(";
      PrintCondition(cond->GetSingleWordInOperand(0u));
      std::cout << ")";
      break;
    // Binary ops
    case SpvOpLogicalOr:
    case SpvOpLogicalAnd:
    case SpvOpIEqual:
    case SpvOpINotEqual:
    case SpvOpSLessThan:
    case SpvOpULessThan:
    case SpvOpSLessThanEqual:
    case SpvOpULessThanEqual:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThanEqual:
    case SpvOpUGreaterThanEqual:
    case SpvOpFOrdEqual:
    case SpvOpFOrdNotEqual:
    case SpvOpFOrdLessThan:
    case SpvOpFOrdGreaterThan:
    case SpvOpFOrdLessThanEqual:
    case SpvOpFOrdGreaterThanEqual:
      PrintCondition(cond->GetSingleWordInOperand(0u));
      std::cout << " " << Operator(cond->opcode()) << " ";
      PrintCondition(cond->GetSingleWordInOperand(1u));
      break;
    case SpvOpFUnordEqual:
    case SpvOpFUnordNotEqual:
    case SpvOpFUnordLessThan:
    case SpvOpFUnordGreaterThan:
    case SpvOpFUnordLessThanEqual:
    case SpvOpFUnordGreaterThanEqual:
      std::cout << "!(";
      PrintCondition(cond->GetSingleWordInOperand(0u));
      std::cout << " " << UnordOperator(cond->opcode()) << " ";
      PrintCondition(cond->GetSingleWordInOperand(1u));
      std::cout << ")";
      break;
    case SpvOpConstant: {
      auto type = context()->get_type_mgr()->GetType(cond->type_id());
      auto constant = context()->get_constant_mgr()->GetConstantFromInst(cond);
      if (type->AsFloat()) {
        std::cout << constant->GetFloat();
      } else {
        std::cout << constant->GetU32();
      }
      break;
    }
    case SpvOpLoad:
      std::cout << "*(";
      PrintCondition(cond->GetSingleWordInOperand(0u));
      std::cout << ")";
      break;
    case SpvOpAccessChain: {
      std::cout << "[";
      cond->ForEachInId([this](const uint32_t* id_ptr) {
        PrintCondition(*id_ptr);
        std::cout << " ";
      });
      std::cout << "]";
      break;
    }
    case SpvOpVariable:
      PrintVariable(cond);
      break;
    default:
      std::cout << "<x>";
      break;
  }
}

void ConditionDependencies::PrintVariable(const Instruction* inst) {
  SpvStorageClass sc =
      static_cast<SpvStorageClass>(inst->GetSingleWordInOperand(0u));
  std::string storage_class(StorageClass(sc));
  uint32_t descriptor_set = 0;
  uint32_t binding = 0;
  for (auto dec : context()->get_decoration_mgr()->GetDecorationsFor(
           inst->result_id(), false)) {
    if (dec->GetSingleWordInOperand(1u) == SpvDecorationDescriptorSet)
      descriptor_set = dec->GetSingleWordInOperand(2u);
    if (dec->GetSingleWordInOperand(1u) == SpvDecorationBinding)
      binding = dec->GetSingleWordInOperand(2u);
  }
  std::cout << storage_class << "(" << descriptor_set << ", " << binding << ")";
}

std::string ConditionDependencies::StorageClass(SpvStorageClass sc) {
  switch (sc) {
    case SpvStorageClassUniformConstant:
      return "UniformConstant";
    case SpvStorageClassInput:
      return "Input";
    case SpvStorageClassUniform:
      return "Uniform";
    case SpvStorageClassOutput:
      return "Output";
    case SpvStorageClassWorkgroup:
      return "Workgroup";
    case SpvStorageClassCrossWorkgroup:
      return "CrossWorkgroup";
    case SpvStorageClassPrivate:
      return "Private";
    case SpvStorageClassFunction:
      return "Function";
    case SpvStorageClassGeneric:
      return "Generic";
    case SpvStorageClassPushConstant:
      return "PushConstant";
    case SpvStorageClassAtomicCounter:
      return "AtomicCounter";
    case SpvStorageClassImage:
      return "Image";
    case SpvStorageClassStorageBuffer:
      return "StorageBuffer";
    default:
      return "<sc>";
  }
}

std::string ConditionDependencies::Operator(SpvOp op) {
  switch (op) {
    case SpvOpLogicalOr:
      return "||";
    case SpvOpLogicalAnd:
      return "&&";
    case SpvOpIEqual:
      return "==";
    case SpvOpINotEqual:
      return "!=";
    case SpvOpSLessThan:
    case SpvOpULessThan:
      return "<";
    case SpvOpSLessThanEqual:
    case SpvOpULessThanEqual:
      return "<=";
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThan:
      return ">";
    case SpvOpSGreaterThanEqual:
    case SpvOpUGreaterThanEqual:
      return ">=";
    case SpvOpFOrdEqual:
    case SpvOpFUnordEqual:
      return "==";
    case SpvOpFOrdNotEqual:
    case SpvOpFUnordNotEqual:
      return "!=";
    case SpvOpFOrdLessThan:
    case SpvOpFUnordLessThan:
      return "<";
    case SpvOpFOrdGreaterThan:
    case SpvOpFUnordGreaterThan:
      return ">";
    case SpvOpFOrdLessThanEqual:
    case SpvOpFUnordLessThanEqual:
      return "<=";
    case SpvOpFOrdGreaterThanEqual:
    case SpvOpFUnordGreaterThanEqual:
      return ">=";
    default:
      return "<unknown op>";
  }
}

std::string ConditionDependencies::UnordOperator(SpvOp op) {
  switch (op) {
    case SpvOpFUnordEqual:
      return "!=*";
    case SpvOpFUnordNotEqual:
      return "==*";
    case SpvOpFUnordLessThan:
      return ">=*";
    case SpvOpFUnordGreaterThan:
      return "<=*";
    case SpvOpFUnordLessThanEqual:
      return ">*";
    case SpvOpFUnordGreaterThanEqual:
      return "<*";
    default:
      return "<unknown op>";
  }
}

}  // namespace opt
}  // namespace spvtools
