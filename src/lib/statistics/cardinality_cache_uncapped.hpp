#pragma once

#include "cardinality_cache.hpp"

#include "abstract_engaged_cardinality_cache.hpp"

namespace opossum {

class CardinalityCacheUncapped : public AbstractEngagedCardinalityCache {
 public:
  std::shared_ptr<CardinalityCacheEntry> get(const BaseJoinGraph &join_graph) override;
  std::optional<KeyValuePair> set(const BaseJoinGraph &join_graph, const std::shared_ptr<CardinalityCacheEntry>& entry) override;
  void visit(const CardinalityCacheVisitor &visitor) const override;
  void clear() override;
  size_t size() const override;

 private:
  std::unordered_map<BaseJoinGraph, std::shared_ptr<CardinalityCacheEntry>> _cache;
};


}  // namespace opossum