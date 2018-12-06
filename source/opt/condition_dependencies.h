#ifndef SOURCE_OPT_CONDITION_DEPENDENCIES_H_
#define SOURCE_OPT_CONDITION_DEPENDENCIES_H_

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
  void PrintVariable(const Instruction* inst);
  std::string StorageClass(SpvStorageClass sc);
  std::string Operator(SpvOp op);
  std::string UnordOperator(SpvOp op);
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_CONDITION_DEPENDENCIES_H_
