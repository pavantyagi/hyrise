#pragma once

#include <boost/lexical_cast.hpp>
#include <string>

#include "all_type_variant.hpp"
#include "expression/parameter_expression.hpp"
#include "logical_query_plan/lqp_column_reference.hpp"
#include "types.hpp"

namespace opossum {

namespace hana = boost::hana;

/**
 * AllParameterVariant holds either an AllTypeVariant, a ColumnID or a Placeholder.
 * It should be used to generalize Opossum operator calls.
 */

// This holds pairs of all types and their respective string representation
static constexpr auto parameter_types =
    hana::make_tuple(hana::make_pair("AllTypeVariant", hana::type_c<AllTypeVariant>),
                     hana::make_pair("ColumnID", hana::type_c<ColumnID>),                      // NOLINT
                     hana::make_pair("LQPColumnReference", hana::type_c<LQPColumnReference>),  // NOLINT
                     hana::make_pair("Parameter", hana::type_c<ParameterID>));                 // NOLINT

// This holds only the possible data types.
static constexpr auto parameter_types_as_hana_sequence = hana::transform(parameter_types, hana::second);  // NOLINT

// Convert tuple to mpl vector
using ParameterTypesAsMplVector =
    decltype(hana::to<hana::ext::boost::mpl::vector_tag>(parameter_types_as_hana_sequence));

// Create boost::variant from mpl vector
using AllParameterVariantNonExplicit = typename boost::make_variant_over<ParameterTypesAsMplVector>::type;

struct AllParameterVariant : public AllParameterVariantNonExplicit {
  AllParameterVariant() : AllParameterVariantNonExplicit() {}

  template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AllParameterVariant> && !std::is_same_v<std::decay_t<T>, AllTypeVariant>>>
  explicit AllParameterVariant(const T& value) : AllParameterVariantNonExplicit(value) {}

  template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AllParameterVariant> && !std::is_same_v<std::decay_t<T>, AllTypeVariant>>>
  explicit AllParameterVariant(T&& value) : AllParameterVariantNonExplicit(std::forward<T>(value)) {}

  // We do allow implicit comparisons from AllTypeVariant to AllParameterVariant
  AllParameterVariant(const AllTypeVariant& variant) : AllParameterVariantNonExplicit(variant) {}
  AllParameterVariant(AllTypeVariant&& variant) : AllParameterVariantNonExplicit(std::forward<AllTypeVariant>(variant)) {}

  // No need for a destructor here, because the base destructor will be called. Rule of five does not apply.
  AllParameterVariant(const AllParameterVariant& variant)
      : AllParameterVariantNonExplicit(static_cast<const AllParameterVariantNonExplicit&>(variant)) {}
  AllParameterVariant(AllParameterVariant&& variant)
      : AllParameterVariantNonExplicit(static_cast<AllParameterVariantNonExplicit&&>(
            static_cast<AllParameterVariantNonExplicit&&>(std::forward<AllParameterVariant>(variant)))) {}
  AllParameterVariant& operator=(const AllParameterVariant& variant) {
    return static_cast<AllParameterVariant&>(
        AllParameterVariantNonExplicit::operator=(static_cast<const AllParameterVariantNonExplicit&>(variant)));
  }
  AllParameterVariant& operator=(AllParameterVariant&& variant) {
    return static_cast<AllParameterVariant&>(
        AllParameterVariantNonExplicit::operator=(static_cast<AllParameterVariantNonExplicit&&>(
            static_cast<AllParameterVariantNonExplicit&&>(std::forward<AllParameterVariant>(variant)))));
  }

  bool operator==(const AllParameterVariant& other) const {
    return static_cast<const AllParameterVariantNonExplicit&>(*this) ==
           static_cast<const AllParameterVariantNonExplicit&>(other);
  }

  bool operator!=(const AllParameterVariant& other) const {
    return static_cast<const AllParameterVariantNonExplicit&>(*this) !=
           static_cast<const AllParameterVariantNonExplicit&>(other);
  }
};

// Function to check if AllParameterVariant is AllTypeVariant
inline bool is_variant(const AllParameterVariant& variant) { return (variant.type() == typeid(AllTypeVariant)); }

// Function to check if AllParameterVariant is a column id
inline bool is_column_id(const AllParameterVariant& variant) { return (variant.type() == typeid(ColumnID)); }

// Function to check if AllParameterVariant is an LQP column reference
inline bool is_lqp_column_reference(const AllParameterVariant& variant) {
  return (variant.type() == typeid(LQPColumnReference));
}

// Function to check if AllParameterVariant is a ParameterID
inline bool is_parameter_id(const AllParameterVariant& variant) { return (variant.type() == typeid(ParameterID)); }

std::string to_string(const AllParameterVariant& x);

}  // namespace opossum
