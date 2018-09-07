#include "equal_num_elements_histogram.hpp"

#include <memory>
#include <numeric>

#include "histogram_utils.hpp"
#include "storage/table.hpp"
#include "storage/value_column.hpp"

namespace opossum {

template <typename T>
EqualNumElementsHistogram<T>::EqualNumElementsHistogram(const std::vector<T>& mins, const std::vector<T>& maxs,
                                                        const std::vector<uint64_t>& counts,
                                                        const uint64_t distinct_count_per_bin,
                                                        const uint64_t num_bins_with_extra_value)
    : AbstractHistogram<T>(),
      _mins(mins),
      _maxs(maxs),
      _counts(counts),
      _distinct_count_per_bin(distinct_count_per_bin),
      _num_bins_with_extra_value(num_bins_with_extra_value) {}

template <>
EqualNumElementsHistogram<std::string>::EqualNumElementsHistogram(
    const std::vector<std::string>& mins, const std::vector<std::string>& maxs, const std::vector<uint64_t>& counts,
    const uint64_t distinct_count_per_bin, const uint64_t num_bins_with_extra_value,
    const std::string& supported_characters, const uint64_t string_prefix_length)
    : AbstractHistogram<std::string>(supported_characters, string_prefix_length),
      _mins(mins),
      _maxs(maxs),
      _counts(counts),
      _distinct_count_per_bin(distinct_count_per_bin),
      _num_bins_with_extra_value(num_bins_with_extra_value) {
  for (auto bin_id = 0u; bin_id < mins.size(); bin_id++) {
    Assert(mins[bin_id].find_first_not_of(supported_characters) == std::string::npos, "Unsupported characters.");
    Assert(maxs[bin_id].find_first_not_of(supported_characters) == std::string::npos, "Unsupported characters.");
  }
}

template <typename T>
EqualNumElementsBinStats<T> EqualNumElementsHistogram<T>::_get_bin_stats(
    const std::vector<std::pair<T, uint64_t>>& value_counts, const size_t max_num_bins) {
  // If there are fewer distinct values than the number of desired bins use that instead.
  const auto distinct_count = value_counts.size();
  const auto num_bins = distinct_count < max_num_bins ? static_cast<size_t>(distinct_count) : max_num_bins;

  // Split values evenly among bins.
  const auto distinct_count_per_bin = distinct_count / num_bins;
  const auto num_bins_with_extra_value = distinct_count % num_bins;

  std::vector<T> mins;
  std::vector<T> maxs;
  std::vector<uint64_t> counts;
  mins.reserve(num_bins);
  maxs.reserve(num_bins);
  counts.reserve(num_bins);

  auto begin_index = 0ul;
  for (BinID bin_index = 0; bin_index < num_bins; bin_index++) {
    auto end_index = begin_index + distinct_count_per_bin - 1;
    if (bin_index < num_bins_with_extra_value) {
      end_index++;
    }

    const auto current_min = value_counts[begin_index].first;
    const auto current_max = value_counts[end_index].first;

    mins.emplace_back(current_min);
    maxs.emplace_back(current_max);
    counts.emplace_back(std::accumulate(value_counts.cbegin() + begin_index, value_counts.cbegin() + end_index + 1,
                                        uint64_t{0},
                                        [](uint64_t a, const std::pair<T, uint64_t>& b) { return a + b.second; }));

    begin_index = end_index + 1;
  }

  return {mins, maxs, counts, distinct_count_per_bin, num_bins_with_extra_value};
}

template <typename T>
std::shared_ptr<EqualNumElementsHistogram<T>> EqualNumElementsHistogram<T>::from_column(
    const std::shared_ptr<const BaseColumn>& column, const size_t max_num_bins,
    const std::optional<std::string>& supported_characters, const std::optional<uint64_t>& string_prefix_length) {
  std::string characters;
  uint64_t prefix_length;
  if constexpr (std::is_same_v<T, std::string>) {
    const auto pair = AbstractHistogram<T>::_get_or_check_prefix_settings(supported_characters, string_prefix_length);
    characters = pair.first;
    prefix_length = pair.second;
  }

  const auto value_counts = AbstractHistogram<T>::_calculate_value_counts(column);

  if (value_counts.empty()) {
    return nullptr;
  }

  const auto bin_stats = EqualNumElementsHistogram<T>::_get_bin_stats(value_counts, max_num_bins);

  if constexpr (std::is_same_v<T, std::string>) {
    return std::make_shared<EqualNumElementsHistogram<T>>(
        bin_stats.mins, bin_stats.maxs, bin_stats.counts, bin_stats.distinct_count_per_bin,
        bin_stats.num_bins_with_extra_value, characters, prefix_length);
  } else {
    return std::make_shared<EqualNumElementsHistogram<T>>(bin_stats.mins, bin_stats.maxs, bin_stats.counts,
                                                          bin_stats.distinct_count_per_bin,
                                                          bin_stats.num_bins_with_extra_value);
  }
}

template <typename T>
std::shared_ptr<AbstractHistogram<T>> EqualNumElementsHistogram<T>::clone() const {
  return std::make_shared<EqualNumElementsHistogram<T>>(_mins, _maxs, _counts, _distinct_count_per_bin,
                                                        _num_bins_with_extra_value);
}

template <>
std::shared_ptr<AbstractHistogram<std::string>> EqualNumElementsHistogram<std::string>::clone() const {
  return std::make_shared<EqualNumElementsHistogram<std::string>>(_mins, _maxs, _counts, _distinct_count_per_bin,
                                                                  _num_bins_with_extra_value, _supported_characters,
                                                                  _string_prefix_length);
}

template <typename T>
HistogramType EqualNumElementsHistogram<T>::histogram_type() const {
  return HistogramType::EqualNumElements;
}

template <typename T>
size_t EqualNumElementsHistogram<T>::num_bins() const {
  return _counts.size();
}

template <typename T>
BinID EqualNumElementsHistogram<T>::_bin_for_value(const T value) const {
  const auto it = std::lower_bound(_maxs.begin(), _maxs.end(), value);
  const auto index = static_cast<BinID>(std::distance(_maxs.begin(), it));

  if (it == _maxs.end() || value < _bin_min(index) || value > _bin_max(index)) {
    return INVALID_BIN_ID;
  }

  return index;
}

template <typename T>
BinID EqualNumElementsHistogram<T>::_upper_bound_for_value(const T value) const {
  const auto it = std::upper_bound(_maxs.begin(), _maxs.end(), value);
  const auto index = static_cast<BinID>(std::distance(_maxs.begin(), it));

  if (it == _maxs.end()) {
    return INVALID_BIN_ID;
  }

  return index;
}

template <typename T>
T EqualNumElementsHistogram<T>::_bin_min(const BinID index) const {
  DebugAssert(index < _mins.size(), "Index is not a valid bin.");
  return _mins[index];
}

template <typename T>
T EqualNumElementsHistogram<T>::_bin_max(const BinID index) const {
  DebugAssert(index < _maxs.size(), "Index is not a valid bin.");
  return _maxs[index];
}

template <typename T>
uint64_t EqualNumElementsHistogram<T>::_bin_count(const BinID index) const {
  DebugAssert(index < _counts.size(), "Index is not a valid bin.");
  return _counts[index];
}

template <typename T>
uint64_t EqualNumElementsHistogram<T>::_bin_count_distinct(const BinID index) const {
  return _distinct_count_per_bin + (index < _num_bins_with_extra_value ? 1 : 0);
}

template <typename T>
uint64_t EqualNumElementsHistogram<T>::total_count() const {
  return std::accumulate(_counts.begin(), _counts.end(), 0ul);
}

template <typename T>
uint64_t EqualNumElementsHistogram<T>::total_count_distinct() const {
  return _distinct_count_per_bin * num_bins() + _num_bins_with_extra_value;
}

EXPLICITLY_INSTANTIATE_DATA_TYPES(EqualNumElementsHistogram);

}  // namespace opossum
