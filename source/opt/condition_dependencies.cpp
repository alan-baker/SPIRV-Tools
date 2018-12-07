// Copyright (c) 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "source/opt/condition_dependencies.h"

#include <iostream>

#include "source/opt/function.h"

namespace spvtools {
namespace opt {

Pass::Status ConditionDependencies::Process() {
  for (auto& function : *get_module()) {
    for (auto& block : function) {
      uint32_t merge_id = block.MergeBlockIdIfAny();
      if (merge_id) merge_to_header_[merge_id] = block.id();
    }
  }
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
    case SpvOpIAdd:
    case SpvOpFAdd:
    case SpvOpISub:
    case SpvOpFSub:
    case SpvOpIMul:
    case SpvOpFMul:
    case SpvOpSDiv:
    case SpvOpUDiv:
    case SpvOpFDiv:
    case SpvOpBitwiseAnd:
    case SpvOpBitwiseOr:
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
      std::cout << "(";
      PrintCondition(cond->GetSingleWordInOperand(0u));
      std::cout << " " << Operator(cond->opcode()) << " ";
      PrintCondition(cond->GetSingleWordInOperand(1u));
      std::cout << ")";
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
    case SpvOpConstantTrue:
      std::cout << "true";
      break;
    case SpvOpConstantFalse:
      std::cout << "false";
      break;
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
    case SpvOpPhi:
      PrintPhi(cond);
      break;
    case SpvOpExtInst:
      PrintExtInst(cond);
      break;
    default:
      std::cout << "<%" << cond->result_id() << ">";
      break;
  }
}

void ConditionDependencies::PrintVariable(Instruction* inst) {
  SpvStorageClass sc =
      static_cast<SpvStorageClass>(inst->GetSingleWordInOperand(0u));
  std::string storage_class(StorageClass(sc));
  uint32_t descriptor_set = 0;
  uint32_t binding = 0;
  std::string builtin;
  for (auto dec : context()->get_decoration_mgr()->GetDecorationsFor(
           inst->result_id(), false)) {
    if (dec->GetSingleWordInOperand(1u) == SpvDecorationDescriptorSet)
      descriptor_set = dec->GetSingleWordInOperand(2u);
    if (dec->GetSingleWordInOperand(1u) == SpvDecorationBinding)
      binding = dec->GetSingleWordInOperand(2u);
    if (dec->GetSingleWordInOperand(1u) == SpvDecorationBuiltIn)
      builtin =
          BuiltIn(static_cast<SpvBuiltIn>(dec->GetSingleWordInOperand(2u)));
  }
  std::cout << storage_class << "(";
  if (descriptor_set != 0) {
    std::cout << descriptor_set << ", " << binding;
  } else {
    std::cout << builtin;
  }
  std::cout << ")";
}

void ConditionDependencies::PrintPhi(Instruction* inst) {
  auto block = context()->get_instr_block(inst);
  auto iter = merge_to_header_.find(block->id());
  if (iter == merge_to_header_.end()) {
    std::cout << "<%" << inst->result_id() << ">";
    return;
  }
  std::cout << "(";
  auto& header = *block->GetParent()->FindBlock(iter->second);
  auto branch = header.terminator();
  assert(branch->opcode() == SpvOpBranchConditional);
  PrintCondition(header.terminator()->GetSingleWordInOperand(0u));
  std::cout << " ? ";
  auto left_id = inst->GetSingleWordInOperand(0u);
  auto left_block = inst->GetSingleWordInOperand(1u);
  auto right_id = inst->GetSingleWordInOperand(2u);
  // auto right_block = inst->GetSingleWordInOperand(3u);
  bool reverse = true;
  if (left_block == branch->GetSingleWordInOperand(1u) ||
      left_block == header.id())
    reverse = false;
  PrintCondition(reverse ? right_id : left_id);
  std::cout << " : ";
  PrintCondition(reverse ? left_id : right_id);
  std::cout << ")";
}

void ConditionDependencies::PrintExtInst(Instruction* inst) {
  auto import = get_def_use_mgr()->GetDef(inst->GetSingleWordInOperand(0u));
  std::string inst_set =
      reinterpret_cast<const char*>(import->GetOperand(1u).words.data());
  if (inst_set == "GLSL.std.450") {
    GLSLstd450 ext_op =
        static_cast<GLSLstd450>(inst->GetSingleWordInOperand(1u));
    switch (ext_op) {
      case GLSLstd450FAbs:
        std::cout << "|";
        PrintCondition(inst->GetSingleWordInOperand(2u));
        std::cout << "|";
        break;
      default:
        std::cout << "<%" << inst->result_id() << ">";
        break;
    }
  } else {
    std::cout << "<%" << inst->result_id() << ">";
  }
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

std::string ConditionDependencies::BuiltIn(SpvBuiltIn builtin) {
  switch (builtin) {
    case SpvBuiltInGlobalInvocationId:
      return "GlobalInvocationId";
    default:
      return "<builtin>";
  }
}

std::string ConditionDependencies::Operator(SpvOp op) {
  switch (op) {
    case SpvOpIAdd:
    case SpvOpFAdd:
      return "+";
    case SpvOpFSub:
    case SpvOpISub:
      return "-";
    case SpvOpIMul:
    case SpvOpFMul:
      return "*";
    case SpvOpSDiv:
    case SpvOpUDiv:
    case SpvOpFDiv:
      return "/";
    case SpvOpBitwiseAnd:
      return "&";
    case SpvOpBitwiseOr:
      return "|";
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
