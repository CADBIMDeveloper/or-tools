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

#ifndef OR_TOOLS_MATH_OPT_CPP_SOLUTION_H_
#define OR_TOOLS_MATH_OPT_CPP_SOLUTION_H_

// IWYU pragma: private, include "ortools/math_opt/cpp/math_opt.h"

#include <optional>

#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "ortools/math_opt/core/model_storage.h"
#include "ortools/math_opt/cpp/enums.h"  // IWYU pragma: export
#include "ortools/math_opt/cpp/linear_constraint.h"
#include "ortools/math_opt/cpp/variable_and_expressions.h"
#include "ortools/math_opt/result.pb.h"  // IWYU pragma: export
#include "ortools/math_opt/solution.pb.h"

namespace operations_research {
namespace math_opt {

// Feasibility of a primal or dual solution as claimed by the solver.
enum class SolutionStatus {
  // Solver does not claim a feasibility status.
  kUndetermined = SOLUTION_STATUS_UNDETERMINED,

  // Solver claims the solution is feasible.
  kFeasible = SOLUTION_STATUS_FEASIBLE,

  // Solver claims the solution is infeasible.
  kInfeasible = SOLUTION_STATUS_INFEASIBLE,
};

MATH_OPT_DEFINE_ENUM(SolutionStatus, SOLUTION_STATUS_UNSPECIFIED);

// Status of a variable/constraint in a LP basis.
enum class BasisStatus : int8_t {
  // The variable/constraint is free (it has no finite bounds).
  kFree = BASIS_STATUS_FREE,

  // The variable/constraint is at its lower bound (which must be finite).
  kAtLowerBound = BASIS_STATUS_AT_LOWER_BOUND,

  // The variable/constraint is at its upper bound (which must be finite).
  kAtUpperBound = BASIS_STATUS_AT_UPPER_BOUND,

  // The variable/constraint has identical finite lower and upper bounds.
  kFixedValue = BASIS_STATUS_FIXED_VALUE,

  // The variable/constraint is basic.
  kBasic = BASIS_STATUS_BASIC,
};

MATH_OPT_DEFINE_ENUM(BasisStatus, BASIS_STATUS_UNSPECIFIED);

// A solution to an optimization problem.
//
// E.g. consider a simple linear program:
//   min c * x
//   s.t. A * x >= b
//   x >= 0.
// A primal solution is assignment values to x. It is feasible if it satisfies
// A * x >= b and x >= 0 from above. In the class PrimalSolution,
// variable_values is x and objective_value is c * x.
//
// For the general case of a MathOpt optimization model, see
// go/mathopt-solutions for details.
struct PrimalSolution {
  static PrimalSolution FromProto(
      const ModelStorage* model,
      const PrimalSolutionProto& primal_solution_proto);

  VariableMap<double> variable_values;
  double objective_value = 0.0;

  SolutionStatus feasibility_status = SolutionStatus::kUndetermined;
};

// A direction of unbounded improvement to an optimization problem;
// equivalently, a certificate of infeasibility for the dual of the
// optimization problem.
//
// E.g. consider a simple linear program:
//   min c * x
//   s.t. A * x >= b
//   x >= 0
// A primal ray is an x that satisfies:
//   c * x < 0
//   A * x >= 0
//   x >= 0
// Observe that given a feasible solution, any positive multiple of the primal
// ray plus that solution is still feasible, and gives a better objective
// value. A primal ray also proves the dual optimization problem infeasible.
//
// In the class PrimalRay, variable_values is this x.
//
// For the general case of a MathOpt optimization model, see
// go/mathopt-solutions for details.
struct PrimalRay {
  static PrimalRay FromProto(const ModelStorage* model,
                             const PrimalRayProto& primal_ray_proto);

  VariableMap<double> variable_values;
};

// A solution to the dual of an optimization problem.
//
// E.g. consider the primal dual pair linear program pair:
//   (Primal)             (Dual)
//   min c * x            max b * y
//   s.t. A * x >= b      s.t. y * A + r = c
//   x >= 0               y, r >= 0.
// The dual solution is the pair (y, r). It is feasible if it satisfies the
// constraints from (Dual) above.
//
// Below, y is dual_values, r is reduced_costs, and b * y is objective value.
//
// For the general case, see go/mathopt-solutions and go/mathopt-dual (and
// note that the dual objective depends on r in the general case).
struct DualSolution {
  static DualSolution FromProto(const ModelStorage* model,
                                const DualSolutionProto& dual_solution_proto);

  LinearConstraintMap<double> dual_values;
  VariableMap<double> reduced_costs;
  std::optional<double> objective_value;

  SolutionStatus feasibility_status = SolutionStatus::kUndetermined;
};

// A direction of unbounded improvement to the dual of an optimization,
// problem; equivalently, a certificate of primal infeasibility.
//
// E.g. consider the primal dual pair linear program pair:
//    (Primal)              (Dual)
//    min c * x             max b * y
//    s.t. A * x >= b       s.t. y * A + r = c
//    x >= 0                y, r >= 0.
// The dual ray is the pair (y, r) satisfying:
//   b * y > 0
//   y * A + r = 0
//   y, r >= 0
// Observe that adding a positive multiple of (y, r) to dual feasible solution
// maintains dual feasibility and improves the objective (proving the dual is
// unbounded). The dual ray also proves the primal problem is infeasible.
//
// In the class DualRay, y is dual_values and r is reduced_costs.
//
// For the general case, see go/mathopt-solutions and go/mathopt-dual (and
// note that the dual objective depends on r in the general case).
struct DualRay {
  static DualRay FromProto(const ModelStorage* model,
                           const DualRayProto& dual_ray_proto);

  LinearConstraintMap<double> dual_values;
  VariableMap<double> reduced_costs;
};

// A combinatorial characterization for a solution to a linear program.
//
// The simplex method for solving linear programs always returns a "basic
// feasible solution" which can be described combinatorially as a Basis. A
// basis assigns a BasisStatus for every variable and linear constraint.
//
// E.g. consider a standard form LP:
//   min c * x
//   s.t. A * x = b
//   x >= 0
// that has more variables than constraints and with full row rank A.
//
// Let n be the number of variables and m the number of linear constraints. A
// valid basis for this problem can be constructed as follows:
//  * All constraints will have basis status FIXED.
//  * Pick m variables such that the columns of A are linearly independent and
//    assign the status BASIC.
//  * Assign the status AT_LOWER for the remaining n - m variables.
//
// The basic solution for this basis is the unique solution of A * x = b that
// has all variables with status AT_LOWER fixed to their lower bounds (all
// zero). The resulting solution is called a basic feasible solution if it
// also satisfies x >= 0.
//
// See go/mathopt-basis for treatment of the general case and an explanation
// of how a dual solution is determined for a basis.
struct Basis {
  // Returns a Basis built from the input indexed_basis, CHECKing that no
  // values is BASIS_STATUS_UNSPECIFIED. No check is done on other values so
  // out of bounds values e.g. BasisStatusProto_MAX+1 won't raise an
  // assertion. See SpaseBasisStatusVectorIsValid().
  static Basis FromProto(const ModelStorage* model,
                         const BasisProto& basis_proto);

  LinearConstraintMap<BasisStatus> constraint_status;
  VariableMap<BasisStatus> variable_status;

  // This is an advanced status. For single-sided LPs it should be equal to the
  // feasibility status of the associated dual solution. For two-sided LPs it
  // may be different in some edge cases (e.g. incomplete solves with primal
  // simplex). For more details see go/mathopt-basis-advanced#dualfeasibility.
  SolutionStatus basic_dual_feasibility = SolutionStatus::kUndetermined;
};

// What is included in a solution depends on the kind of problem and solver.
// The current common patterns are
//   1. MIP solvers return only a primal solution.
//   2. Simplex LP solvers often return a basis and the primal and dual
//      solutions associated to this basis.
//   3. Other continuous solvers often return a primal and dual solution
//      solution that are connected in a solver-dependent form.
struct Solution {
  static Solution FromProto(const ModelStorage* model,
                            const SolutionProto& solution_proto);
  std::optional<PrimalSolution> primal_solution;
  std::optional<DualSolution> dual_solution;
  std::optional<Basis> basis;
};

}  // namespace math_opt
}  // namespace operations_research

#endif  // OR_TOOLS_MATH_OPT_CPP_SOLUTION_H_
