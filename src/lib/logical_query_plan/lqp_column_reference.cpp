#include "lqp_column_reference.hpp"

#include "boost/functional/hash.hpp"

#include "abstract_lqp_node.hpp"
#include "utils/assert.hpp"

namespace opossum {

LQPColumnReference::LQPColumnReference(const std::shared_ptr<const AbstractLQPNode>& original_node,
                                       ColumnID original_column_id)
    : _original_node(original_node), _original_column_id(original_column_id) {}

std::shared_ptr<const AbstractLQPNode> LQPColumnReference::original_node() const { return _original_node.lock(); }

ColumnID LQPColumnReference::original_column_id() const { return _original_column_id; }

std::string LQPColumnReference::description() const {
  const auto node = this->original_node();

  DebugAssert(node, "LQPColumnReference state not sufficient to retrieve column name");
  return node->get_verbose_column_name(_original_column_id);
}

size_t LQPColumnReference::hash() const {
  DebugAssert(original_node(), "original_node needs to exist for hashing to work");

  auto hash = original_node()->hash();
  boost::hash_combine(hash, _original_column_id.t);
  return hash;
}

bool LQPColumnReference::operator==(const LQPColumnReference& rhs) const {
  return original_node() == rhs.original_node() && _original_column_id == rhs._original_column_id;
}

std::ostream& operator<<(std::ostream& os, const LQPColumnReference& column_reference) {
  const auto node = column_reference.original_node();

  if (column_reference.original_node()) {
    os << column_reference.description();
  } else {
    os << "[Invalid LQPColumnReference, ColumnID:" << column_reference.original_column_id() << "]";
  }
  return os;
}
}  // namespace opossum

namespace std {

size_t hash<opossum::LQPColumnReference>::operator()(const opossum::LQPColumnReference& lqp_column_reference) const {
  return lqp_column_reference.hash();
}

} // namespace std