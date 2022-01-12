// Copyright 2010-2021 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ortools/math_opt/cpp/variable_and_expressions.h"

#include <string>
#include <utility>
#include <vector>

#include "ortools/base/logging.h"
#include "absl/base/attributes.h"
#include "ortools/base/map_util.h"
#include "ortools/base/int_type.h"
#include "ortools/math_opt/core/model_storage.h"
#include "ortools/math_opt/cpp/key_types.h"

namespace operations_research {
namespace math_opt {

#ifdef MATH_OPT_USE_EXPRESSION_COUNTERS
LinearExpression::LinearExpression() { ++num_calls_default_constructor_; }

LinearExpression::LinearExpression(const LinearExpression& other)
    : terms_(other.terms_), offset_(other.offset_) {
  ++num_calls_copy_constructor_;
}

LinearExpression::LinearExpression(LinearExpression&& other)
    : terms_(std::move(other.terms_)),
      offset_(std::exchange(other.offset_, 0.0)) {
  ++num_calls_move_constructor_;
}

LinearExpression& LinearExpression::operator=(const LinearExpression& other) {
  terms_ = other.terms_;
  offset_ = other.offset_;
  return *this;
}

ABSL_CONST_INIT thread_local int
    LinearExpression::num_calls_default_constructor_ = 0;
ABSL_CONST_INIT thread_local int LinearExpression::num_calls_copy_constructor_ =
    0;
ABSL_CONST_INIT thread_local int LinearExpression::num_calls_move_constructor_ =
    0;
ABSL_CONST_INIT thread_local int
    LinearExpression::num_calls_initializer_list_constructor_ = 0;

void LinearExpression::ResetCounters() {
  num_calls_default_constructor_ = 0;
  num_calls_copy_constructor_ = 0;
  num_calls_move_constructor_ = 0;
  num_calls_initializer_list_constructor_ = 0;
}
#endif  // MATH_OPT_USE_EXPRESSION_COUNTERS

double LinearExpression::Evaluate(
    const VariableMap<double>& variable_values) const {
  if (variable_values.storage() != nullptr && storage() != nullptr) {
    CHECK_EQ(variable_values.storage(), storage())
        << internal::kObjectsFromOtherModelStorage;
  }
  double result = offset_;
  for (const auto& [variable_id, coef] : terms_.raw_map()) {
    result += coef * variable_values.raw_map().at(variable_id);
  }
  return result;
}

double LinearExpression::EvaluateWithDefaultZero(
    const VariableMap<double>& variable_values) const {
  if (variable_values.storage() != nullptr && storage() != nullptr) {
    CHECK_EQ(variable_values.storage(), storage())
        << internal::kObjectsFromOtherModelStorage;
  }
  double result = offset_;
  for (const auto& [variable_id, coef] : terms_.raw_map()) {
    result +=
        coef * gtl::FindWithDefault(variable_values.raw_map(), variable_id);
  }
  return result;
}

std::ostream& operator<<(std::ostream& ostr,
                         const LinearExpression& expression) {
  // TODO(b/169415597): improve linear expression format:
  //  - use bijective formatting in base10 of the double factors.
  //  - handle negative coefficients, replacing `... + -3*x ` by `... - 3*x`.
  //  - make sure to quote the variable name so that we support:
  //    * variable names contains +, -, ...
  //    * variable names resembling anonymous variable names.
  const std::vector<Variable> sorted_variables = expression.terms_.SortedKeys();
  bool first = true;
  for (const auto v : sorted_variables) {
    if (first) {
      first = false;
    } else {
      ostr << " + ";
    }
    ostr << expression.terms_.at(v) << "*";
    const std::string& name =
        expression.terms_.storage()->variable_name(v.typed_id());
    if (name.empty()) {
      ostr << "[" << v << "]";
    } else {
      ostr << name;
    }
  }

  if (!first) {
    ostr << " + ";
  }
  ostr << expression.offset();

  return ostr;
}

std::ostream& operator<<(std::ostream& ostr,
                         const BoundedLinearExpression& bounded_expression) {
  // TODO(b/170991498): use bijective conversion from double to base-10 string
  // to make sure we can reproduce bugs.
  ostr << bounded_expression.lower_bound
       << " <= " << bounded_expression.expression
       << " <= " << bounded_expression.upper_bound;
  return ostr;
}

double QuadraticExpression::Evaluate(
    const VariableMap<double>& variable_values) const {
  if (variable_values.storage() != nullptr && storage() != nullptr) {
    CHECK_EQ(variable_values.storage(), storage())
        << internal::kObjectsFromOtherModelStorage;
  }
  double result = offset();
  for (const auto& [variable_id, coef] : linear_terms_.raw_map()) {
    result += coef * variable_values.raw_map().at(variable_id);
  }
  for (const auto& [variable_ids, coef] : quadratic_terms_.raw_map()) {
    result += coef * variable_values.raw_map().at(variable_ids.first) *
              variable_values.raw_map().at(variable_ids.second);
  }
  return result;
}

double QuadraticExpression::EvaluateWithDefaultZero(
    const VariableMap<double>& variable_values) const {
  if (variable_values.storage() != nullptr && storage() != nullptr) {
    CHECK_EQ(variable_values.storage(), storage())
        << internal::kObjectsFromOtherModelStorage;
  }
  double result = offset();
  for (const auto& [variable_id, coef] : linear_terms_.raw_map()) {
    result +=
        coef * gtl::FindWithDefault(variable_values.raw_map(), variable_id);
  }
  for (const auto& [variable_ids, coef] : quadratic_terms_.raw_map()) {
    result +=
        coef *
        gtl::FindWithDefault(variable_values.raw_map(), variable_ids.first) *
        gtl::FindWithDefault(variable_values.raw_map(), variable_ids.second);
  }
  return result;
}

std::ostream& operator<<(std::ostream& ostr, const QuadraticExpression& expr) {
  // TODO(b/169415597): improve quadratic expression formatting.
  bool first = true;
  for (const auto v : expr.quadratic_terms().SortedKeys()) {
    if (first) {
      first = false;
    } else {
      ostr << " + ";
    }
    ostr << expr.quadratic_terms().at(v) << "*";
    const Variable first_variable(expr.quadratic_terms().storage(),
                                  v.typed_id().first);
    const Variable second_variable(expr.quadratic_terms().storage(),
                                   v.typed_id().second);
    if (first_variable == second_variable) {
      ostr << first_variable << "²";
    } else {
      ostr << first_variable << "*" << second_variable;
    }
  }
  for (const auto v : expr.linear_terms().SortedKeys()) {
    if (first) {
      first = false;
    } else {
      ostr << " + ";
    }
    ostr << expr.linear_terms().at(v) << "*" << v;
  }

  if (!first) {
    ostr << " + ";
  }
  ostr << expr.offset();

  return ostr;
}

#ifdef MATH_OPT_USE_EXPRESSION_COUNTERS
QuadraticExpression::QuadraticExpression() { ++num_calls_default_constructor_; }

QuadraticExpression::QuadraticExpression(const QuadraticExpression& other)
    : quadratic_terms_(other.quadratic_terms_),
      linear_terms_(other.linear_terms_),
      offset_(other.offset_) {
  ++num_calls_copy_constructor_;
}

QuadraticExpression::QuadraticExpression(QuadraticExpression&& other)
    : quadratic_terms_(std::move(other.quadratic_terms_)),
      linear_terms_(std::move(other.linear_terms_)),
      offset_(std::exchange(other.offset_, 0.0)) {
  ++num_calls_move_constructor_;
}

QuadraticExpression& QuadraticExpression::operator=(
    const QuadraticExpression& other) {
  quadratic_terms_ = other.quadratic_terms_;
  linear_terms_ = other.linear_terms_;
  offset_ = other.offset_;
  return *this;
}

ABSL_CONST_INIT thread_local int
    QuadraticExpression::num_calls_default_constructor_ = 0;
ABSL_CONST_INIT thread_local int
    QuadraticExpression::num_calls_copy_constructor_ = 0;
ABSL_CONST_INIT thread_local int
    QuadraticExpression::num_calls_move_constructor_ = 0;
ABSL_CONST_INIT thread_local int
    QuadraticExpression::num_calls_initializer_list_constructor_ = 0;
ABSL_CONST_INIT thread_local int
    QuadraticExpression::num_calls_linear_expression_constructor_ = 0;

void QuadraticExpression::ResetCounters() {
  num_calls_default_constructor_ = 0;
  num_calls_copy_constructor_ = 0;
  num_calls_move_constructor_ = 0;
  num_calls_initializer_list_constructor_ = 0;
  num_calls_linear_expression_constructor_ = 0;
}
#endif  // MATH_OPT_USE_EXPRESSION_COUNTERS

}  // namespace math_opt
}  // namespace operations_research
