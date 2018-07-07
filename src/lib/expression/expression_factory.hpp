#pragma once

#include <memory>

#include "abstract_expression.hpp"
#include "aggregate_expression.hpp"
#include "arithmetic_expression.hpp"
#include "between_expression.hpp"
#include "binary_predicate_expression.hpp"
#include "case_expression.hpp"
#include "exists_expression.hpp"
#include "extract_expression.hpp"
#include "function_expression.hpp"
#include "in_expression.hpp"
#include "is_null_expression.hpp"
#include "list_expression.hpp"
#include "logical_expression.hpp"
#include "lqp_column_expression.hpp"
#include "lqp_select_expression.hpp"
#include "negate_expression.hpp"
#include "parameter_expression.hpp"
#include "pqp_select_expression.hpp"
#include "value_expression.hpp"

/**
 * This file provides convenience methods to create (nested) Expression objects with little boilerplate
 *
 * So this
 *     const auto value_123 = std::make_shared<ValueExpression>(123);
 *     const auto value_1234 = std::make_shared<ValueExpression>(1234);
 *     const auto a_eq_123 = std::make_shared<BinaryPredicateExpression>(PredicateCondition::Equals, int_float_a_expression, value_123);
 *     const auto a_eq_1234 = std::make_shared<BinaryPredicateExpression>(PredicateCondition::Equals, int_float_a_expression, value_1234);
 *     const auto null_value = std::make_shared<ValueExpression>(NullValue{});
 *     const auto case_a_eq_1234 = std::make_shared<CaseExpression>(a_eq_1234, int_float_a_expression, null_value);
 *     const auto case_a_eq_123 = std::make_shared<CaseExpression>(a_eq_123, int_float_b_expression, case_a_eq_1234);
 *
 *  becomes
 *      case_(equals(a, 123),
 *            b,
 *            case_(equals(a, 1234),
 *                  a,
 *                  null()))
 */

namespace opossum {

class AbstractOperator;
class LQPColumnReference;

namespace expression_factory {

// to_expression() overload that just forwards
std::shared_ptr<AbstractExpression> to_expression(const std::shared_ptr<AbstractExpression>& expression);

std::shared_ptr<LQPColumnExpression> to_expression(const LQPColumnReference& column_reference);
std::shared_ptr<ValueExpression> to_expression(const AllTypeVariant& value);

std::shared_ptr<ValueExpression> value(const AllTypeVariant& value);
std::shared_ptr<ValueExpression> null();

template <auto t, typename E>
struct unary final {
  template <typename A>
  std::shared_ptr<E> operator()(const A& a) const {
    return std::make_shared<E>(t, to_expression(a));
  };
};

template <auto t, typename E>
struct binary final {
  template <typename A, typename B>
  std::shared_ptr<E> operator()(const A& a, const B& b) const {
    return std::make_shared<E>(t, to_expression(a), to_expression(b));
  };
};

template <typename E>
struct ternary final {
  template <typename A, typename B, typename C>
  std::shared_ptr<E> operator()(const A& a, const B& b, const C& c) const {
    return std::make_shared<E>(to_expression(a), to_expression(b), to_expression(c));
  };
};

extern unary<PredicateCondition::IsNull, IsNullExpression> is_null;
extern unary<PredicateCondition::IsNotNull, IsNullExpression> is_not_null;
extern unary<AggregateFunction::Sum, AggregateExpression> sum;
extern unary<AggregateFunction::Max, AggregateExpression> max;
extern unary<AggregateFunction::Min, AggregateExpression> min;
extern unary<AggregateFunction::Avg, AggregateExpression> avg;
extern unary<AggregateFunction::Count, AggregateExpression> count;
extern unary<AggregateFunction::CountDistinct, AggregateExpression> count_distinct;

extern binary<ArithmeticOperator::Division, ArithmeticExpression> div_;
extern binary<ArithmeticOperator::Multiplication, ArithmeticExpression> mul;
extern binary<ArithmeticOperator::Addition, ArithmeticExpression> add;
extern binary<ArithmeticOperator::Subtraction, ArithmeticExpression> sub;
extern binary<ArithmeticOperator::Modulo, ArithmeticExpression> mod;
extern binary<PredicateCondition::Like, BinaryPredicateExpression> like;
extern binary<PredicateCondition::NotLike, BinaryPredicateExpression> not_like;
extern binary<PredicateCondition::Equals, BinaryPredicateExpression> equals;
extern binary<PredicateCondition::NotEquals, BinaryPredicateExpression> not_equals;
extern binary<PredicateCondition::LessThan, BinaryPredicateExpression> less_than;
extern binary<PredicateCondition::LessThanEquals, BinaryPredicateExpression> less_than_equals;
extern binary<PredicateCondition::GreaterThanEquals, BinaryPredicateExpression> greater_than_equals;
extern binary<PredicateCondition::GreaterThan, BinaryPredicateExpression> greater_than;
extern binary<LogicalOperator::And, LogicalExpression> and_;
extern binary<LogicalOperator::Or, LogicalExpression> or_;

extern ternary<BetweenExpression> between;
extern ternary<CaseExpression> case_;

template <typename... Args>
std::shared_ptr<LQPSelectExpression> select(const std::shared_ptr<AbstractLQPNode>& lqp,
                                            Args&&... parameter_id_expression_pairs) {
  if constexpr (sizeof...(Args) > 0) {
    // Correlated subselect
    return std::make_shared<LQPSelectExpression>(
        lqp, std::vector<ParameterID>{{parameter_id_expression_pairs.first...}},
        std::vector<std::shared_ptr<AbstractExpression>>{{to_expression(parameter_id_expression_pairs.second)...}});
  } else {
    // Not corrcelated
    return std::make_shared<LQPSelectExpression>(lqp, std::vector<ParameterID>{},
                                                 std::vector<std::shared_ptr<AbstractExpression>>{});
  }
}

template <typename... Args>
std::shared_ptr<PQPSelectExpression> select(const std::shared_ptr<AbstractOperator>& pqp, const DataType data_type,
                                            const bool nullable, Args&&... parameter_id_column_id_pairs) {
  if constexpr (sizeof...(Args) > 0) {
    // Correlated subselect
    return std::make_shared<PQPSelectExpression>(
        pqp, data_type, nullable,
        std::vector<std::pair<ParameterID, ColumnID>>{
            {std::make_pair(parameter_id_column_id_pairs.first, parameter_id_column_id_pairs.second)...}});
  } else {
    // Not correlated
    return std::make_shared<PQPSelectExpression>(pqp, data_type, nullable);
  }
}

template <typename... Args>
std::vector<std::shared_ptr<AbstractExpression>> expression_vector(Args&&... args) {
  return std::vector<std::shared_ptr<AbstractExpression>>({to_expression(args)...});
}

template <typename String, typename Start, typename Length>
std::shared_ptr<FunctionExpression> substr(const String& string, const Start& start, const Length& length) {
  return std::make_shared<FunctionExpression>(
      FunctionType::Substring, expression_vector(to_expression(string), to_expression(start), to_expression(length)));
}

template <typename... Args>
std::shared_ptr<FunctionExpression> concat(const Args... args) {
  return std::make_shared<FunctionExpression>(FunctionType::Concatenate, expression_vector(to_expression(args)...));
}

template <typename... Args>
std::shared_ptr<ListExpression> list(Args&&... args) {
  return std::make_shared<ListExpression>(expression_vector(std::forward<Args>(args)...));
}

template <typename V, typename S>
std::shared_ptr<InExpression> in(const V& v, const S& s) {
  return std::make_shared<InExpression>(to_expression(v), to_expression(s));
}

std::shared_ptr<ExistsExpression> exists(const std::shared_ptr<AbstractExpression>& select_expression);

template <typename F>
std::shared_ptr<ExtractExpression> extract(const DatetimeComponent datetime_component, const F& from) {
  return std::make_shared<ExtractExpression>(datetime_component, to_expression(from));
}

std::shared_ptr<ParameterExpression> parameter(const ParameterID parameter_id);
std::shared_ptr<LQPColumnExpression> column(const LQPColumnReference& column_reference);

template <typename ReferencedExpression>
std::shared_ptr<ParameterExpression> parameter(const ParameterID parameter_id, const ReferencedExpression& referenced) {
  return std::make_shared<ParameterExpression>(parameter_id, *to_expression(referenced));
}

std::shared_ptr<AggregateExpression> count_star();

template <typename Argument>
std::shared_ptr<NegateExpression> negate(const Argument& argument) {
  return std::make_shared<NegateExpression>(to_expression(argument));
}

}  // namespace expression_factory

}  // namespace opossum
