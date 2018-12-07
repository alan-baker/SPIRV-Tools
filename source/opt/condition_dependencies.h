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

#ifndef SOURCE_OPT_CONDITION_DEPENDENCIES_H_
#define SOURCE_OPT_CONDITION_DEPENDENCIES_H_

#include <iostream>

#include "source/opt/instruction.h"
#include "source/opt/pass.h"

namespace spvtools {
namespace opt {

class ConditionDependencies : public Pass {
 public:
  const char* name() const override { return "condition-dependencies"; }
  Status Process() override;

 private:
  void PrintCondition(uint32_t cond_id);
  void PrintVariable(Instruction* inst);
  void PrintPhi(Instruction* inst);
  void PrintExtInst(Instruction* inst);
  std::string StorageClass(SpvStorageClass sc);
  std::string BuiltIn(SpvBuiltIn builtin);
  std::string Operator(SpvOp op);
  std::string UnordOperator(SpvOp op);

  std::unordered_map<uint32_t, uint32_t> merge_to_header_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_CONDITION_DEPENDENCIES_H_
