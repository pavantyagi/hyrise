#pragma once

#include "abstract_expression.hpp"

namespace opossum {

enum class FunctionType {
  Substring, Extract
};

class FunctionExpression : public AbstractExpression {
 public:
  FunctionExpression(const FunctionType function_type,
                      const std::vector<std::shared_ptr<AbstractExpression>>& arguments);

  /**
   * @defgroup Overrides for AbstractExpression
   * @{
   */
  bool deep_equals(const AbstractExpression& expression) const override;
  std::shared_ptr<AbstractExpression> deep_copy() const override;
  std::shared_ptr<AbstractExpression> deep_resolve_column_expressions() override;
  /**@}*/

  FunctionType function_type;
  std::vector<std::shared_ptr<AbstractExpression>> arguments;
};

} // namespace opossum