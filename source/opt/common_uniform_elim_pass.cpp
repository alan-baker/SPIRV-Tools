// Copyright (c) 2017 The Khronos Group Inc.
// Copyright (c) 2017 Valve Corporation
// Copyright (c) 2017 LunarG Inc.
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

#include "common_uniform_elim_pass.h"
#include "cfa.h"
#include "ir_context.h"

namespace spvtools {
namespace opt {

namespace {

const uint32_t kAccessChainPtrIdInIdx = 0;
const uint32_t kTypePointerStorageClassInIdx = 0;
const uint32_t kTypePointerTypeIdInIdx = 1;
const uint32_t kConstantValueInIdx = 0;
const uint32_t kExtractCompositeIdInIdx = 0;
const uint32_t kExtractIdx0InIdx = 1;
const uint32_t kStorePtrIdInIdx = 0;
const uint32_t kLoadPtrIdInIdx = 0;
const uint32_t kCopyObjectOperandInIdx = 0;
const uint32_t kTypeIntWidthInIdx = 0;

}  // anonymous namespace

bool CommonUniformElimPass::IsNonPtrAccessChain(const SpvOp opcode) const {
  return opcode == SpvOpAccessChain || opcode == SpvOpInBoundsAccessChain;
}

bool CommonUniformElimPass::IsSamplerOrImageType(
    const ir::Instruction* typeInst) const {
  switch (typeInst->opcode()) {
    case SpvOpTypeSampler:
    case SpvOpTypeImage:
    case SpvOpTypeSampledImage:
      return true;
    default:
      break;
  }
  if (typeInst->opcode() != SpvOpTypeStruct) return false;
  // Return true if any member is a sampler or image
  int samplerOrImageCnt = 0;
  typeInst->ForEachInId([&samplerOrImageCnt, this](const uint32_t* tid) {
    const ir::Instruction* compTypeInst = get_def_use_mgr()->GetDef(*tid);
    if (IsSamplerOrImageType(compTypeInst)) ++samplerOrImageCnt;
  });
  return samplerOrImageCnt > 0;
}

bool CommonUniformElimPass::IsSamplerOrImageVar(uint32_t varId) const {
  const ir::Instruction* varInst = get_def_use_mgr()->GetDef(varId);
  assert(varInst->opcode() == SpvOpVariable);
  const uint32_t varTypeId = varInst->type_id();
  const ir::Instruction* varTypeInst = get_def_use_mgr()->GetDef(varTypeId);
  const uint32_t varPteTypeId =
      varTypeInst->GetSingleWordInOperand(kTypePointerTypeIdInIdx);
  ir::Instruction* varPteTypeInst = get_def_use_mgr()->GetDef(varPteTypeId);
  return IsSamplerOrImageType(varPteTypeInst);
}

ir::Instruction* CommonUniformElimPass::GetPtr(ir::Instruction* ip,
                                               uint32_t* objId) {
  const SpvOp op = ip->opcode();
  assert(op == SpvOpStore || op == SpvOpLoad);
  *objId = ip->GetSingleWordInOperand(op == SpvOpStore ? kStorePtrIdInIdx
                                                       : kLoadPtrIdInIdx);
  ir::Instruction* ptrInst = get_def_use_mgr()->GetDef(*objId);
  while (ptrInst->opcode() == SpvOpCopyObject) {
    *objId = ptrInst->GetSingleWordInOperand(kCopyObjectOperandInIdx);
    ptrInst = get_def_use_mgr()->GetDef(*objId);
  }
  ir::Instruction* objInst = ptrInst;
  while (objInst->opcode() != SpvOpVariable &&
         objInst->opcode() != SpvOpFunctionParameter) {
    if (IsNonPtrAccessChain(objInst->opcode())) {
      *objId = objInst->GetSingleWordInOperand(kAccessChainPtrIdInIdx);
    } else {
      assert(objInst->opcode() == SpvOpCopyObject);
      *objId = objInst->GetSingleWordInOperand(kCopyObjectOperandInIdx);
    }
    objInst = get_def_use_mgr()->GetDef(*objId);
  }
  return ptrInst;
}

bool CommonUniformElimPass::IsVolatileStruct(uint32_t type_id) {
  assert(get_def_use_mgr()->GetDef(type_id)->opcode() == SpvOpTypeStruct);
  bool has_volatile_deco = false;
  dec_mgr_->ForEachDecoration(type_id, SpvDecorationVolatile,
                              [&has_volatile_deco](const ir::Instruction&) {
                                has_volatile_deco = true;
                              });
  return has_volatile_deco;
}

bool CommonUniformElimPass::IsAccessChainToVolatileStructType(
    const ir::Instruction& AccessChainInst) {
  assert(AccessChainInst.opcode() == SpvOpAccessChain);

  uint32_t ptr_id = AccessChainInst.GetSingleWordInOperand(0);
  const ir::Instruction* ptr_inst = get_def_use_mgr()->GetDef(ptr_id);
  uint32_t pointee_type_id = GetPointeeTypeId(ptr_inst);
  const uint32_t num_operands = AccessChainInst.NumOperands();

  // walk the type tree:
  for (uint32_t idx = 3; idx < num_operands; ++idx) {
    ir::Instruction* pointee_type = get_def_use_mgr()->GetDef(pointee_type_id);

    switch (pointee_type->opcode()) {
      case SpvOpTypeMatrix:
      case SpvOpTypeVector:
      case SpvOpTypeArray:
      case SpvOpTypeRuntimeArray:
        pointee_type_id = pointee_type->GetSingleWordOperand(1);
        break;
      case SpvOpTypeStruct:
        // check for volatile decorations:
        if (IsVolatileStruct(pointee_type_id)) return true;

        if (idx < num_operands - 1) {
          const uint32_t index_id = AccessChainInst.GetSingleWordOperand(idx);
          const ir::Instruction* index_inst =
              get_def_use_mgr()->GetDef(index_id);
          uint32_t index_value = index_inst->GetSingleWordOperand(
              2);  // TODO: replace with GetUintValueFromConstant()
          pointee_type_id = pointee_type->GetSingleWordInOperand(index_value);
        }
        break;
      default:
        assert(false && "Unhandled pointee type.");
    }
  }
  return false;
}

bool CommonUniformElimPass::IsVolatileLoad(const ir::Instruction& loadInst) {
  assert(loadInst.opcode() == SpvOpLoad);
  // Check if this Load instruction has Volatile Memory Access flag
  if (loadInst.NumOperands() == 4) {
    uint32_t memory_access_mask = loadInst.GetSingleWordOperand(3);
    if (memory_access_mask & SpvMemoryAccessVolatileMask) return true;
  }
  // If we load a struct directly (result type is struct),
  // check if the struct is decorated volatile
  uint32_t type_id = loadInst.type_id();
  if (get_def_use_mgr()->GetDef(type_id)->opcode() == SpvOpTypeStruct)
    return IsVolatileStruct(type_id);
  else
    return false;
}

bool CommonUniformElimPass::IsUniformVar(uint32_t varId) {
  const ir::Instruction* varInst =
      get_def_use_mgr()->id_to_defs().find(varId)->second;
  if (varInst->opcode() != SpvOpVariable) return false;
  const uint32_t varTypeId = varInst->type_id();
  const ir::Instruction* varTypeInst =
      get_def_use_mgr()->id_to_defs().find(varTypeId)->second;
  return varTypeInst->GetSingleWordInOperand(kTypePointerStorageClassInIdx) ==
             SpvStorageClassUniform ||
         varTypeInst->GetSingleWordInOperand(kTypePointerStorageClassInIdx) ==
             SpvStorageClassUniformConstant;
}

bool CommonUniformElimPass::HasUnsupportedDecorates(uint32_t id) const {
  analysis::UseList* uses = get_def_use_mgr()->GetUses(id);
  if (uses == nullptr) return false;
  for (auto u : *uses) {
    const SpvOp op = u.inst->opcode();
    if (IsNonTypeDecorate(op)) return true;
  }
  return false;
}

bool CommonUniformElimPass::HasOnlyNamesAndDecorates(uint32_t id) const {
  analysis::UseList* uses = get_def_use_mgr()->GetUses(id);
  if (uses == nullptr) return true;
  for (auto u : *uses) {
    const SpvOp op = u.inst->opcode();
    if (op != SpvOpName && !IsNonTypeDecorate(op)) return false;
  }
  return true;
}

void CommonUniformElimPass::KillNamesAndDecorates(uint32_t id) {
  // TODO(greg-lunarg): Remove id from any OpGroupDecorate and
  // kill if no other operands.
  analysis::UseList* uses = get_def_use_mgr()->GetUses(id);
  if (uses == nullptr) return;
  std::list<ir::Instruction*> killList;
  for (auto u : *uses) {
    const SpvOp op = u.inst->opcode();
    if (op != SpvOpName && !IsNonTypeDecorate(op)) continue;
    killList.push_back(u.inst);
  }
  for (auto kip : killList) get_def_use_mgr()->KillInst(kip);
}

void CommonUniformElimPass::KillNamesAndDecorates(ir::Instruction* inst) {
  // TODO(greg-lunarg): Remove inst from any OpGroupDecorate and
  // kill if not other operands.
  const uint32_t rId = inst->result_id();
  if (rId == 0) return;
  KillNamesAndDecorates(rId);
}

void CommonUniformElimPass::DeleteIfUseless(ir::Instruction* inst) {
  const uint32_t resId = inst->result_id();
  assert(resId != 0);
  if (HasOnlyNamesAndDecorates(resId)) {
    KillNamesAndDecorates(resId);
    get_def_use_mgr()->KillInst(inst);
  }
}

void CommonUniformElimPass::ReplaceAndDeleteLoad(ir::Instruction* loadInst,
                                                 uint32_t replId,
                                                 ir::Instruction* ptrInst) {
  const uint32_t loadId = loadInst->result_id();
  KillNamesAndDecorates(loadId);
  (void)get_def_use_mgr()->ReplaceAllUsesWith(loadId, replId);
  // remove load instruction
  get_def_use_mgr()->KillInst(loadInst);
  // if access chain, see if it can be removed as well
  if (IsNonPtrAccessChain(ptrInst->opcode())) DeleteIfUseless(ptrInst);
}

void CommonUniformElimPass::GenACLoadRepl(
    const ir::Instruction* ptrInst,
    std::vector<std::unique_ptr<ir::Instruction>>* newInsts,
    uint32_t* resultId) {
  // Build and append Load
  const uint32_t ldResultId = TakeNextId();
  const uint32_t varId =
      ptrInst->GetSingleWordInOperand(kAccessChainPtrIdInIdx);
  const ir::Instruction* varInst = get_def_use_mgr()->GetDef(varId);
  assert(varInst->opcode() == SpvOpVariable);
  const uint32_t varPteTypeId = GetPointeeTypeId(varInst);
  std::vector<ir::Operand> load_in_operands;
  load_in_operands.push_back(
      ir::Operand(spv_operand_type_t::SPV_OPERAND_TYPE_ID,
                  std::initializer_list<uint32_t>{varId}));
  std::unique_ptr<ir::Instruction> newLoad(new ir::Instruction(
      SpvOpLoad, varPteTypeId, ldResultId, load_in_operands));
  get_def_use_mgr()->AnalyzeInstDefUse(&*newLoad);
  newInsts->emplace_back(std::move(newLoad));

  // Build and append Extract
  const uint32_t extResultId = TakeNextId();
  const uint32_t ptrPteTypeId = GetPointeeTypeId(ptrInst);
  std::vector<ir::Operand> ext_in_opnds;
  ext_in_opnds.push_back(
      ir::Operand(spv_operand_type_t::SPV_OPERAND_TYPE_ID,
                  std::initializer_list<uint32_t>{ldResultId}));
  uint32_t iidIdx = 0;
  ptrInst->ForEachInId([&iidIdx, &ext_in_opnds, this](const uint32_t* iid) {
    if (iidIdx > 0) {
      const ir::Instruction* cInst = get_def_use_mgr()->GetDef(*iid);
      uint32_t val = cInst->GetSingleWordInOperand(kConstantValueInIdx);
      ext_in_opnds.push_back(
          ir::Operand(spv_operand_type_t::SPV_OPERAND_TYPE_LITERAL_INTEGER,
                      std::initializer_list<uint32_t>{val}));
    }
    ++iidIdx;
  });
  std::unique_ptr<ir::Instruction> newExt(new ir::Instruction(
      SpvOpCompositeExtract, ptrPteTypeId, extResultId, ext_in_opnds));
  get_def_use_mgr()->AnalyzeInstDefUse(&*newExt);
  newInsts->emplace_back(std::move(newExt));
  *resultId = extResultId;
}

bool CommonUniformElimPass::IsConstantIndexAccessChain(ir::Instruction* acp) {
  uint32_t inIdx = 0;
  uint32_t nonConstCnt = 0;
  acp->ForEachInId([&inIdx, &nonConstCnt, this](uint32_t* tid) {
    if (inIdx > 0) {
      ir::Instruction* opInst = get_def_use_mgr()->GetDef(*tid);
      if (opInst->opcode() != SpvOpConstant) ++nonConstCnt;
    }
    ++inIdx;
  });
  return nonConstCnt == 0;
}

bool CommonUniformElimPass::UniformAccessChainConvert(ir::Function* func) {
  bool modified = false;
  for (auto bi = func->begin(); bi != func->end(); ++bi) {
    for (auto ii = bi->begin(); ii != bi->end(); ++ii) {
      if (ii->opcode() != SpvOpLoad) continue;
      uint32_t varId;
      ir::Instruction* ptrInst = GetPtr(&*ii, &varId);
      if (!IsNonPtrAccessChain(ptrInst->opcode())) continue;
      // Do not convert nested access chains
      if (ptrInst->GetSingleWordInOperand(kAccessChainPtrIdInIdx) != varId)
        continue;
      if (!IsUniformVar(varId)) continue;
      if (!IsConstantIndexAccessChain(ptrInst)) continue;
      if (HasUnsupportedDecorates(ii->result_id())) continue;
      if (HasUnsupportedDecorates(ptrInst->result_id())) continue;
      if (IsVolatileLoad(*ii)) continue;
      if (IsAccessChainToVolatileStructType(*ptrInst)) continue;
      std::vector<std::unique_ptr<ir::Instruction>> newInsts;
      uint32_t replId;
      GenACLoadRepl(ptrInst, &newInsts, &replId);
      ReplaceAndDeleteLoad(&*ii, replId, ptrInst);
      ++ii;
      ii = ii.InsertBefore(std::move(newInsts));
      ++ii;
      modified = true;
    }
  }
  return modified;
}

void CommonUniformElimPass::ComputeStructuredSuccessors(ir::Function* func) {
  block2structured_succs_.clear();
  for (auto& blk : *func) {
    // If header, make merge block first successor.
    uint32_t mbid = blk.MergeBlockIdIfAny();
    if (mbid != 0) {
      block2structured_succs_[&blk].push_back(cfg()->block(mbid));
      uint32_t cbid = blk.ContinueBlockIdIfAny();
      if (cbid != 0) {
        block2structured_succs_[&blk].push_back(cfg()->block(mbid));
      }
    }
    // add true successors
    blk.ForEachSuccessorLabel([&blk, this](uint32_t sbid) {
      block2structured_succs_[&blk].push_back(cfg()->block(sbid));
    });
  }
}

void CommonUniformElimPass::ComputeStructuredOrder(
    ir::Function* func, std::list<ir::BasicBlock*>* order) {
  // Compute structured successors and do DFS
  ComputeStructuredSuccessors(func);
  auto ignore_block = [](cbb_ptr) {};
  auto ignore_edge = [](cbb_ptr, cbb_ptr) {};
  auto get_structured_successors = [this](const ir::BasicBlock* block) {
    return &(block2structured_succs_[block]);
  };
  // TODO(greg-lunarg): Get rid of const_cast by making moving const
  // out of the cfa.h prototypes and into the invoking code.
  auto post_order = [&](cbb_ptr b) {
    order->push_front(const_cast<ir::BasicBlock*>(b));
  };

  order->clear();
  spvtools::CFA<ir::BasicBlock>::DepthFirstTraversal(
      &*func->begin(), get_structured_successors, ignore_block, post_order,
      ignore_edge);
}

bool CommonUniformElimPass::CommonUniformLoadElimination(ir::Function* func) {
  // Process all blocks in structured order. This is just one way (the
  // simplest?) to keep track of the most recent block outside of control
  // flow, used to copy common instructions, guaranteed to dominate all
  // following load sites.
  std::list<ir::BasicBlock*> structuredOrder;
  ComputeStructuredOrder(func, &structuredOrder);
  uniform2load_id_.clear();
  bool modified = false;
  // Find insertion point in first block to copy non-dominating loads.
  auto insertItr = func->begin()->begin();
  while (insertItr->opcode() == SpvOpVariable ||
         insertItr->opcode() == SpvOpNop)
    ++insertItr;
  uint32_t mergeBlockId = 0;
  for (auto bi = structuredOrder.begin(); bi != structuredOrder.end(); ++bi) {
    ir::BasicBlock* bp = *bi;
    // Check if we are exiting outermost control construct. If so, remember
    // new load insertion point. Trying to keep register pressure down.
    if (mergeBlockId == bp->id()) {
      mergeBlockId = 0;
      insertItr = bp->begin();
    }
    for (auto ii = bp->begin(); ii != bp->end(); ++ii) {
      if (ii->opcode() != SpvOpLoad) continue;
      uint32_t varId;
      ir::Instruction* ptrInst = GetPtr(&*ii, &varId);
      if (ptrInst->opcode() != SpvOpVariable) continue;
      if (!IsUniformVar(varId)) continue;
      if (IsSamplerOrImageVar(varId)) continue;
      if (HasUnsupportedDecorates(ii->result_id())) continue;
      if (IsVolatileLoad(*ii)) continue;
      uint32_t replId;
      const auto uItr = uniform2load_id_.find(varId);
      if (uItr != uniform2load_id_.end()) {
        replId = uItr->second;
      } else {
        if (mergeBlockId == 0) {
          // Load is in dominating block; just remember it
          uniform2load_id_[varId] = ii->result_id();
          continue;
        } else {
          // Copy load into most recent dominating block and remember it
          replId = TakeNextId();
          std::unique_ptr<ir::Instruction> newLoad(new ir::Instruction(
              SpvOpLoad, ii->type_id(), replId,
              {{spv_operand_type_t::SPV_OPERAND_TYPE_ID, {varId}}}));
          get_def_use_mgr()->AnalyzeInstDefUse(&*newLoad);
          insertItr = insertItr.InsertBefore(std::move(newLoad));
          ++insertItr;
          uniform2load_id_[varId] = replId;
        }
      }
      ReplaceAndDeleteLoad(&*ii, replId, ptrInst);
      modified = true;
    }
    // If we are outside of any control construct and entering one, remember
    // the id of the merge block
    if (mergeBlockId == 0) {
      mergeBlockId = bp->MergeBlockIdIfAny();
    }
  }
  return modified;
}

bool CommonUniformElimPass::CommonUniformLoadElimBlock(ir::Function* func) {
  bool modified = false;
  for (auto& blk : *func) {
    uniform2load_id_.clear();
    for (auto ii = blk.begin(); ii != blk.end(); ++ii) {
      if (ii->opcode() != SpvOpLoad) continue;
      uint32_t varId;
      ir::Instruction* ptrInst = GetPtr(&*ii, &varId);
      if (ptrInst->opcode() != SpvOpVariable) continue;
      if (!IsUniformVar(varId)) continue;
      if (!IsSamplerOrImageVar(varId)) continue;
      if (HasUnsupportedDecorates(ii->result_id())) continue;
      if (IsVolatileLoad(*ii)) continue;
      uint32_t replId;
      const auto uItr = uniform2load_id_.find(varId);
      if (uItr != uniform2load_id_.end()) {
        replId = uItr->second;
      } else {
        uniform2load_id_[varId] = ii->result_id();
        continue;
      }
      ReplaceAndDeleteLoad(&*ii, replId, ptrInst);
      modified = true;
    }
  }
  return modified;
}

bool CommonUniformElimPass::CommonExtractElimination(ir::Function* func) {
  // Find all composite ids with duplicate extracts.
  for (auto bi = func->begin(); bi != func->end(); ++bi) {
    for (auto ii = bi->begin(); ii != bi->end(); ++ii) {
      if (ii->opcode() != SpvOpCompositeExtract) continue;
      // TODO(greg-lunarg): Support multiple indices
      if (ii->NumInOperands() > 2) continue;
      if (HasUnsupportedDecorates(ii->result_id())) continue;
      uint32_t compId = ii->GetSingleWordInOperand(kExtractCompositeIdInIdx);
      uint32_t idx = ii->GetSingleWordInOperand(kExtractIdx0InIdx);
      comp2idx2inst_[compId][idx].push_back(&*ii);
    }
  }
  // For all defs of ids with duplicate extracts, insert new extracts
  // after def, and replace and delete old extracts
  bool modified = false;
  for (auto bi = func->begin(); bi != func->end(); ++bi) {
    for (auto ii = bi->begin(); ii != bi->end(); ++ii) {
      const auto cItr = comp2idx2inst_.find(ii->result_id());
      if (cItr == comp2idx2inst_.end()) continue;
      for (auto idxItr : cItr->second) {
        if (idxItr.second.size() < 2) continue;
        uint32_t replId = TakeNextId();
        std::unique_ptr<ir::Instruction> newExtract(
            new ir::Instruction(*idxItr.second.front()));
        newExtract->SetResultId(replId);
        get_def_use_mgr()->AnalyzeInstDefUse(&*newExtract);
        ++ii;
        ii = ii.InsertBefore(std::move(newExtract));
        for (auto instItr : idxItr.second) {
          uint32_t resId = instItr->result_id();
          KillNamesAndDecorates(resId);
          (void)get_def_use_mgr()->ReplaceAllUsesWith(resId, replId);
          get_def_use_mgr()->KillInst(instItr);
        }
        modified = true;
      }
    }
  }
  return modified;
}

bool CommonUniformElimPass::EliminateCommonUniform(ir::Function* func) {
  bool modified = false;
  modified |= UniformAccessChainConvert(func);
  modified |= CommonUniformLoadElimination(func);
  modified |= CommonExtractElimination(func);

  modified |= CommonUniformLoadElimBlock(func);
  return modified;
}

void CommonUniformElimPass::Initialize(ir::IRContext* c) {
  InitializeProcessing(c);

  // Clear collections.
  comp2idx2inst_.clear();
  dec_mgr_.reset(new analysis::DecorationManager(get_module()));

  // Initialize extension whitelist
  InitExtensions();
};

bool CommonUniformElimPass::AllExtensionsSupported() const {
  // If any extension not in whitelist, return false
  for (auto& ei : get_module()->extensions()) {
    const char* extName =
        reinterpret_cast<const char*>(&ei.GetInOperand(0).words[0]);
    if (extensions_whitelist_.find(extName) == extensions_whitelist_.end())
      return false;
  }
  return true;
}

Pass::Status CommonUniformElimPass::ProcessImpl() {
  // Assumes all control flow structured.
  // TODO(greg-lunarg): Do SSA rewrite for non-structured control flow
  if (!get_module()->HasCapability(SpvCapabilityShader))
    return Status::SuccessWithoutChange;
  // Assumes logical addressing only
  // TODO(greg-lunarg): Add support for physical addressing
  if (get_module()->HasCapability(SpvCapabilityAddresses))
    return Status::SuccessWithoutChange;
  // Do not process if any disallowed extensions are enabled
  if (!AllExtensionsSupported()) return Status::SuccessWithoutChange;
  // Do not process if module contains OpGroupDecorate. Additional
  // support required in KillNamesAndDecorates().
  // TODO(greg-lunarg): Add support for OpGroupDecorate
  for (auto& ai : get_module()->annotations())
    if (ai.opcode() == SpvOpGroupDecorate) return Status::SuccessWithoutChange;
  // If non-32-bit integer type in module, terminate processing
  // TODO(): Handle non-32-bit integer constants in access chains
  for (const ir::Instruction& inst : get_module()->types_values())
    if (inst.opcode() == SpvOpTypeInt &&
        inst.GetSingleWordInOperand(kTypeIntWidthInIdx) != 32)
      return Status::SuccessWithoutChange;
  // Process entry point functions
  ProcessFunction pfn = [this](ir::Function* fp) {
    return EliminateCommonUniform(fp);
  };
  bool modified = ProcessEntryPointCallTree(pfn, get_module());
  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

CommonUniformElimPass::CommonUniformElimPass() {}

Pass::Status CommonUniformElimPass::Process(ir::IRContext* c) {
  Initialize(c);
  return ProcessImpl();
}

void CommonUniformElimPass::InitExtensions() {
  extensions_whitelist_.clear();
  extensions_whitelist_.insert({
      "SPV_AMD_shader_explicit_vertex_parameter",
      "SPV_AMD_shader_trinary_minmax",
      "SPV_AMD_gcn_shader",
      "SPV_KHR_shader_ballot",
      "SPV_AMD_shader_ballot",
      "SPV_AMD_gpu_shader_half_float",
      "SPV_KHR_shader_draw_parameters",
      "SPV_KHR_subgroup_vote",
      "SPV_KHR_16bit_storage",
      "SPV_KHR_device_group",
      "SPV_KHR_multiview",
      "SPV_NVX_multiview_per_view_attributes",
      "SPV_NV_viewport_array2",
      "SPV_NV_stereo_view_rendering",
      "SPV_NV_sample_mask_override_coverage",
      "SPV_NV_geometry_shader_passthrough",
      "SPV_AMD_texture_gather_bias_lod",
      "SPV_KHR_storage_buffer_storage_class",
      // SPV_KHR_variable_pointers
      //   Currently do not support extended pointer expressions
      "SPV_AMD_gpu_shader_int16",
      "SPV_KHR_post_depth_coverage",
      "SPV_KHR_shader_atomic_counter_ops",
  });
}

}  // namespace opt
}  // namespace spvtools

