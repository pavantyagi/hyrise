#pragma once

#include <memory>

namespace opossum {

class PlanBlock;
class PredicateJoinBlock;

/**
 * Abstract base class for algorithms performing JoinOrdering.
 */
class AbstractJoinOrderingAlgorithm {
 public:
  virtual ~AbstractJoinOrderingAlgorithm() = default;

  /**
   * @param input_block     All subblocks have to be PlanBlocks, so perform optimization bottom up
   */
  virtual std::shared_ptr<PlanBlock> operator()(const std::shared_ptr<PredicateJoinBlock>& input_block) = 0;
};

}  // namespace opossum