#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base_test.hpp"
#include "gtest/gtest.h"

#include "storage/base_column.hpp"
#include "storage/chunk.hpp"
#include "statistics/chunk_statistics/counting_quotient_filter.hpp"
#include "types.hpp"

namespace opossum {

class CountingQuotientFilterTest : public BaseTest {
 protected:
  void SetUp() override {
    string_value_counts["hotel"] = 1;
    string_value_counts["delta"] = 6;
    string_value_counts["frank"] = 2;
    string_value_counts["apple"] = 9;
    string_value_counts["charlie"] = 3;
    string_value_counts["inbox"] = 1;
    string_column = std::make_shared<ValueColumn<std::string>>();
    for (auto value_and_count : string_value_counts) {
      for (size_t i = 0; i < value_and_count.second; i++) {
        string_column->append(value_and_count.first);
      }
    }

    int_value_counts[1] = 54;
    int_value_counts[12] = 43;
    int_value_counts[123] = 32;
    int_value_counts[1234] = 21;
    int_value_counts[12345] = 8;
    int_value_counts[123456] = 6;
    int_column = std::make_shared<ValueColumn<int>>();
    for (auto value_and_count : int_value_counts) {
      for (size_t i = 0; i < value_and_count.second; i++) {
        int_column->append(value_and_count.first);
      }
    }

    string_cqf2 = std::make_shared<CountingQuotientFilter<std::string>>(4, RemainderSize::bits2);
    string_cqf4 = std::make_shared<CountingQuotientFilter<std::string>>(4, RemainderSize::bits4);
    string_cqf8 = std::make_shared<CountingQuotientFilter<std::string>>(4, RemainderSize::bits8);
    string_cqf16 = std::make_shared<CountingQuotientFilter<std::string>>(4, RemainderSize::bits16);
    string_cqf32 = std::make_shared<CountingQuotientFilter<std::string>>(4, RemainderSize::bits32);
    string_cqf2->populate(string_column);
    string_cqf4->populate(string_column);
    string_cqf8->populate(string_column);
    string_cqf16->populate(string_column);
    string_cqf32->populate(string_column);

    int_cqf2 = std::make_shared<CountingQuotientFilter<int>>(4, RemainderSize::bits2);
    int_cqf4 = std::make_shared<CountingQuotientFilter<int>>(4, RemainderSize::bits4);
    int_cqf8 = std::make_shared<CountingQuotientFilter<int>>(4, RemainderSize::bits8);
    int_cqf16 = std::make_shared<CountingQuotientFilter<int>>(4, RemainderSize::bits16);
    int_cqf32 = std::make_shared<CountingQuotientFilter<int>>(4, RemainderSize::bits32);
    int_cqf2->populate(int_column);
    int_cqf4->populate(int_column);
    int_cqf8->populate(int_column);
    int_cqf16->populate(int_column);
    int_cqf32->populate(int_column);
  }

  std::shared_ptr<CountingQuotientFilter<std::string>> string_cqf2;
  std::shared_ptr<CountingQuotientFilter<std::string>> string_cqf4;
  std::shared_ptr<CountingQuotientFilter<std::string>> string_cqf8;
  std::shared_ptr<CountingQuotientFilter<std::string>> string_cqf16;
  std::shared_ptr<CountingQuotientFilter<std::string>> string_cqf32;
  std::shared_ptr<CountingQuotientFilter<int>> int_cqf2;
  std::shared_ptr<CountingQuotientFilter<int>> int_cqf4;
  std::shared_ptr<CountingQuotientFilter<int>> int_cqf8;
  std::shared_ptr<CountingQuotientFilter<int>> int_cqf16;
  std::shared_ptr<CountingQuotientFilter<int>> int_cqf32;
  std::shared_ptr<ValueColumn<std::string>> string_column;
  std::shared_ptr<ValueColumn<int>> int_column;
  std::map<std::string, size_t> string_value_counts;
  std::map<int, size_t> int_value_counts;

  template <typename DataType>
  void test_value_counts(std::shared_ptr<CountingQuotientFilter<DataType>> cqf, std::map<DataType, size_t>
      value_counts) {
    for (auto value_and_count : value_counts) {
        EXPECT_TRUE(cqf->count(value_and_count.first) >= value_and_count.second);
    }
  }

  template <typename DataType>
  void test_can_prune(std::shared_ptr<CountingQuotientFilter<DataType>> cqf, std::map<DataType, size_t>
      value_counts) {
    for (auto value_and_count : value_counts) {
        EXPECT_FALSE(cqf->can_prune(value_and_count.first, PredicateCondition::Equals));
    }
  }
};

TEST_F(CountingQuotientFilterTest, NoUndercountsString) {
  test_value_counts<std::string>(string_cqf2, string_value_counts);
  test_value_counts<std::string>(string_cqf4, string_value_counts);
  test_value_counts<std::string>(string_cqf8, string_value_counts);
  test_value_counts<std::string>(string_cqf16, string_value_counts);
  test_value_counts<std::string>(string_cqf32, string_value_counts);
}

TEST_F(CountingQuotientFilterTest, NoUndercountsInt) {
  test_value_counts<int>(int_cqf2, int_value_counts);
  test_value_counts<int>(int_cqf4, int_value_counts);
  test_value_counts<int>(int_cqf8, int_value_counts);
  test_value_counts<int>(int_cqf16, int_value_counts);
  test_value_counts<int>(int_cqf32, int_value_counts);
}

TEST_F(CountingQuotientFilterTest, CanPruneString) {
  test_can_prune<std::string>(string_cqf2, string_value_counts);
  test_can_prune<std::string>(string_cqf4, string_value_counts);
  test_can_prune<std::string>(string_cqf8, string_value_counts);
  test_can_prune<std::string>(string_cqf16, string_value_counts);
  test_can_prune<std::string>(string_cqf32, string_value_counts);
}

TEST_F(CountingQuotientFilterTest, CanPruneInt) {
  test_can_prune<int>(int_cqf2, int_value_counts);
  test_can_prune<int>(int_cqf4, int_value_counts);
  test_can_prune<int>(int_cqf8, int_value_counts);
  test_can_prune<int>(int_cqf16, int_value_counts);
  test_can_prune<int>(int_cqf32, int_value_counts);
}

}  // namespace opossum
