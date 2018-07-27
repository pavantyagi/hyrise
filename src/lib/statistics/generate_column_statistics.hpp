#pragma once

#include <unordered_set>

#include "base_column_statistics.hpp"
#include "chunk_statistics/histograms/equal_num_elements_histogram.hpp"
#include "column_statistics.hpp"
#include "histogram_column_statistics.hpp"
#include "resolve_type.hpp"
#include "storage/create_iterable_from_column.hpp"
#include "storage/table.hpp"

namespace opossum {

/**
 * Generate the statistics of a single column. Used by generate_table_statistics().
 * If num_bucket is set, create statistics of type HistogramColumnStatistics, otherwise create basic statistics.
 */
template <typename ColumnDataType>
std::shared_ptr<BaseColumnStatistics> generate_column_statistics(const std::shared_ptr<const Table>& table,
                                                                 const ColumnID column_id) {
  std::unordered_set<ColumnDataType> distinct_set;

  auto null_value_count = size_t{0};

  auto min = std::numeric_limits<ColumnDataType>::max();
  auto max = std::numeric_limits<ColumnDataType>::lowest();

  for (ChunkID chunk_id{0}; chunk_id < table->chunk_count(); ++chunk_id) {
    const auto base_column = table->get_chunk(chunk_id)->get_column(column_id);

    resolve_column_type<ColumnDataType>(*base_column, [&](auto& column) {
      auto iterable = create_iterable_from_column<ColumnDataType>(column);
      iterable.for_each([&](const auto& column_value) {
        if (column_value.is_null()) {
          ++null_value_count;
        } else {
          distinct_set.insert(column_value.value());
          min = std::min(min, column_value.value());
          max = std::max(max, column_value.value());
        }
      });
    });
  }

  const auto null_value_ratio =
      table->row_count() > 0 ? static_cast<float>(null_value_count) / static_cast<float>(table->row_count()) : 0.0f;
  const auto distinct_count = static_cast<float>(distinct_set.size());

  if (distinct_count == 0.0f) {
    min = std::numeric_limits<ColumnDataType>::min();
    max = std::numeric_limits<ColumnDataType>::max();
  }

  return std::make_shared<ColumnStatistics<ColumnDataType>>(null_value_ratio, distinct_count, min, max);
}

template <>
std::shared_ptr<BaseColumnStatistics> generate_column_statistics<std::string>(const std::shared_ptr<const Table>& table,
                                                                              const ColumnID column_id);

template <typename ColumnDataType>
std::shared_ptr<BaseColumnStatistics> generate_column_statistics(const std::shared_ptr<const Table>& table,
                                                                 const ColumnID column_id, const int64_t num_buckets) {
  auto histogram = std::make_shared<EqualNumElementsHistogram<ColumnDataType>>(table);
  histogram->generate(column_id, num_buckets > 0 ? static_cast<size_t>(num_buckets) : 100u);

  // TODO(tim): incorporate into Histograms
  const auto null_value_ratio = 0.0f;
  return std::make_shared<HistogramColumnStatistics<ColumnDataType>>(histogram, null_value_ratio);
}

}  // namespace opossum
