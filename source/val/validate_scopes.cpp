// Copyright (c) 2018 Google LLC.
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

#include "source/val/validate_scopes.h"

#include "source/diagnostic.h"
#include "source/spirv_target_env.h"
#include "source/val/instruction.h"
#include "source/val/validation_state.h"

namespace spvtools {
namespace val {

bool IsValidScope(uint32_t scope) {
  // Deliberately avoid a default case so we have to update the list when the
  // scopes list changes.
  switch (static_cast<SpvScope>(scope)) {
    case SpvScopeCrossDevice:
    case SpvScopeDevice:
    case SpvScopeWorkgroup:
    case SpvScopeSubgroup:
    case SpvScopeInvocation:
    case SpvScopeQueueFamilyKHR:
      return true;
    case SpvScopeMax:
      break;
  }
  return false;
}

spv_result_t ValidateExecutionScope(ValidationState_t& _,
                                    const Instruction* inst, uint32_t scope) {
  SpvOp opcode = inst->opcode();
  bool is_int32 = false, is_const_int32 = false;
  uint32_t value = 0;
  std::tie(is_int32, is_const_int32, value) = _.EvalInt32IfConst(scope);

  if (!is_int32) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": expected Execution Scope to be a 32-bit int";
  }

  if (!is_const_int32) {
    if (_.HasCapability(SpvCapabilityShader) &&
        !_.HasCapability(SpvCapabilityCooperativeMatrixNV)) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << "Scope ids must be OpConstant when Shader capability is "
             << "present";
    }
    if (_.HasCapability(SpvCapabilityShader) &&
        _.HasCapability(SpvCapabilityCooperativeMatrixNV) &&
        !spvOpcodeIsConstant(_.GetIdOpcode(scope))) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << "Scope ids must be constant or specialization constant when "
             << "CooperativeMatrixNV capability is present";
    }
    return SPV_SUCCESS;
  }

  if (is_const_int32 && !IsValidScope(value)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << "Invalid scope value:\n " << _.Disassemble(*_.FindDef(scope));
  }

  // Vulkan specific rules
  if (spvIsVulkanEnv(_.context()->target_env)) {
    // Vulkan 1.1 specific rules
    if (_.context()->target_env != SPV_ENV_VULKAN_1_0) {
      // Scope for Non Uniform Group Operations must be limited to Subgroup
      if (spvOpcodeIsNonUniformGroupOperation(opcode) &&
          value != SpvScopeSubgroup) {
        return _.diag(SPV_ERROR_INVALID_DATA, inst)
               << spvOpcodeString(opcode)
               << ": in Vulkan environment Execution scope is limited to "
               << "Subgroup";
      }
    }

    // If OpControlBarrier is used in fragment, vertex, tessellation evaluation,
    // or geometry stages, the execution Scope must be Subgroup.
    if (opcode == SpvOpControlBarrier && value != SpvScopeSubgroup) {
      _.function(inst->function()->id())
          ->RegisterExecutionModelLimitation([](SpvExecutionModel model,
                                                std::string* message) {
            if (model == SpvExecutionModelFragment ||
                model == SpvExecutionModelVertex ||
                model == SpvExecutionModelGeometry ||
                model == SpvExecutionModelTessellationEvaluation) {
              if (message) {
                *message =
                    "in Vulkan evironment, OpControlBarrier execution scope "
                    "must be Subgroup for Fragment, Vertex, Geometry and "
                    "TessellationEvaluation execution models";
              }
              return false;
            }
            return true;
          });
    }

    // Vulkan generic rules
    // Scope for execution must be limited to Workgroup or Subgroup
    if (value != SpvScopeWorkgroup && value != SpvScopeSubgroup) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in Vulkan environment Execution Scope is limited to "
             << "Workgroup and Subgroup";
    }
  }

  // WebGPU Specific rules
  if (spvIsWebGPUEnv(_.context()->target_env)) {
    if (value != SpvScopeWorkgroup) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in WebGPU environment Execution Scope is limited to "
             << "Workgroup";
    }
  }

  // TODO(atgoo@github.com) Add checks for OpenCL and OpenGL environments.

  // General SPIRV rules
  // Scope for execution must be limited to Workgroup or Subgroup for
  // non-uniform operations
  if (spvOpcodeIsNonUniformGroupOperation(opcode) &&
      value != SpvScopeSubgroup && value != SpvScopeWorkgroup) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": Execution scope is limited to Subgroup or Workgroup";
  }

  return SPV_SUCCESS;
}

spv_result_t ValidateMemoryScope(ValidationState_t& _, const Instruction* inst,
                                 uint32_t scope) {
  const SpvOp opcode = inst->opcode();
  bool is_int32 = false, is_const_int32 = false;
  uint32_t value = 0;
  std::tie(is_int32, is_const_int32, value) = _.EvalInt32IfConst(scope);

  if (!is_int32) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": expected Memory Scope to be a 32-bit int";
  }

  if (!is_const_int32) {
    if (_.HasCapability(SpvCapabilityShader) &&
        !_.HasCapability(SpvCapabilityCooperativeMatrixNV)) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << "Scope ids must be OpConstant when Shader capability is "
             << "present";
    }
    if (_.HasCapability(SpvCapabilityShader) &&
        _.HasCapability(SpvCapabilityCooperativeMatrixNV) &&
        !spvOpcodeIsConstant(_.GetIdOpcode(scope))) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << "Scope ids must be constant or specialization constant when "
             << "CooperativeMatrixNV capability is present";
    }
    return SPV_SUCCESS;
  }

  if (is_const_int32 && !IsValidScope(value)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << "Invalid scope value:\n " << _.Disassemble(*_.FindDef(scope));
  }

  if (value == SpvScopeQueueFamilyKHR) {
    if (_.HasCapability(SpvCapabilityVulkanMemoryModelKHR)) {
      return SPV_SUCCESS;
    } else {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": Memory Scope QueueFamilyKHR requires capability "
             << "VulkanMemoryModelKHR";
    }
  }

  if (value == SpvScopeDevice &&
      _.HasCapability(SpvCapabilityVulkanMemoryModelKHR) &&
      !_.HasCapability(SpvCapabilityVulkanMemoryModelDeviceScopeKHR)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << "Use of device scope with VulkanKHR memory model requires the "
           << "VulkanMemoryModelDeviceScopeKHR capability";
  }

  // Vulkan Specific rules
  if (spvIsVulkanEnv(_.context()->target_env)) {
    if (value == SpvScopeCrossDevice) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in Vulkan environment, Memory Scope cannot be CrossDevice";
    }

    if (spvOpcodeIsAtomicOp(inst->opcode()) ||
        inst->opcode() == SpvOpControlBarrier ||
        inst->opcode() == SpvOpMemoryBarrier) {
      uint32_t semantics_index = 0;
      switch (inst->opcode()) {
        case SpvOpControlBarrier:
          semantics_index = 2;
          break;
        case SpvOpMemoryBarrier:
          semantics_index = 1;
          break;
        case SpvOpAtomicStore:
          semantics_index = 2;
          break;
        default:
          // For compare exchanges we only consider equal semantics.
          semantics_index = 4;
          break;
      }
      bool semantics_int32 = false;
      bool semantics_const = false;
      // This is intentionally a bad value.
      uint32_t semantics_value = 0;
      const auto semantics = inst->GetOperandAs<uint32_t>(semantics_index);
      std::tie(semantics_int32, semantics_const, semantics_value) =
          _.EvalInt32IfConst(semantics);

      if (semantics_const && semantics_value != 0 &&
          value == SpvScopeInvocation) {
        return _.diag(SPV_ERROR_INVALID_DATA, inst)
               << "In the Vulkan environment, Invocation memory scope can only "
                  "be used if Memory Semantics are Relaxed";
      }
    } else if (value == SpvScopeInvocation) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in the Vulkan environment, Memory Scope cannot be "
                "Invocation";
    }

    // Vulkan 1.0 specifc rules
    if (_.context()->target_env == SPV_ENV_VULKAN_1_0 &&
        value != SpvScopeDevice && value != SpvScopeWorkgroup &&
        value != SpvScopeInvocation) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in Vulkan 1.0 environment Memory Scope is limited to "
             << "Device, Workgroup and Invocation";
    }
    // Vulkan 1.1 specifc rules
    if (_.context()->target_env == SPV_ENV_VULKAN_1_1 &&
        value != SpvScopeDevice && value != SpvScopeWorkgroup &&
        value != SpvScopeSubgroup && value != SpvScopeInvocation) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": in Vulkan 1.1 environment Memory Scope is limited to "
             << "Device, Workgroup and Invocation";
    }
  }

  // WebGPU specific rules
  if (spvIsWebGPUEnv(_.context()->target_env)) {
    switch (inst->opcode()) {
      case SpvOpControlBarrier:
        if (value != SpvScopeWorkgroup) {
          return _.diag(SPV_ERROR_INVALID_DATA, inst)
                 << spvOpcodeString(opcode)
                 << ": in WebGPU environment Memory Scope is limited to "
                 << "Workgroup for OpControlBarrier";
        }
        break;
      case SpvOpMemoryBarrier:
        if (value != SpvScopeWorkgroup) {
          return _.diag(SPV_ERROR_INVALID_DATA, inst)
                 << spvOpcodeString(opcode)
                 << ": in WebGPU environment Memory Scope is limited to "
                 << "Workgroup for OpMemoryBarrier";
        }
        break;
      default:
        if (spvOpcodeIsAtomicOp(inst->opcode())) {
          if (value != SpvScopeQueueFamilyKHR) {
            return _.diag(SPV_ERROR_INVALID_DATA, inst)
                   << spvOpcodeString(opcode)
                   << ": in WebGPU environment Memory Scope is limited to "
                   << "QueueFamilyKHR for OpAtomic* operations";
          }
        }

        if (value != SpvScopeWorkgroup && value != SpvScopeQueueFamilyKHR) {
          return _.diag(SPV_ERROR_INVALID_DATA, inst)
                 << spvOpcodeString(opcode)
                 << ": in WebGPU environment Memory Scope is limited to "
                 << "Workgroup and QueueFamilyKHR";
        }
        break;
    }
  }

  // TODO(atgoo@github.com) Add checks for OpenCL and OpenGL environments.

  return SPV_SUCCESS;
}

}  // namespace val
}  // namespace spvtools
