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

#include "ortools/sat/scheduling_cuts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "ortools/base/logging.h"
#include "ortools/base/stl_util.h"
#include "ortools/base/strong_vector.h"
#include "ortools/sat/cuts.h"
#include "ortools/sat/diffn_util.h"
#include "ortools/sat/implied_bounds.h"
#include "ortools/sat/integer.h"
#include "ortools/sat/intervals.h"
#include "ortools/sat/linear_constraint.h"
#include "ortools/sat/linear_constraint_manager.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_base.h"
#include "ortools/util/strong_integers.h"

namespace operations_research {
namespace sat {

namespace {

// Minimum amount of violation of the cut constraint by the solution. This
// is needed to avoid numerical issues and adding cuts with minor effect.
const double kMinCutViolation = 1e-4;

// Returns the lp value of a Literal.
double GetLiteralLpValue(
    const Literal lit,
    const absl::StrongVector<IntegerVariable, double>& lp_values,
    const IntegerEncoder* encoder) {
  const IntegerVariable direct_view = encoder->GetLiteralView(lit);
  if (direct_view != kNoIntegerVariable) {
    return lp_values[direct_view];
  }
  const IntegerVariable opposite_view = encoder->GetLiteralView(lit.Negated());
  DCHECK_NE(opposite_view, kNoIntegerVariable);
  return 1.0 - lp_values[opposite_view];
}

void AddIntegerVariableFromIntervals(SchedulingConstraintHelper* helper,
                                     Model* model,
                                     std::vector<IntegerVariable>* vars) {
  IntegerEncoder* encoder = model->GetOrCreate<IntegerEncoder>();
  for (int t = 0; t < helper->NumTasks(); ++t) {
    if (helper->Starts()[t].var != kNoIntegerVariable) {
      vars->push_back(helper->Starts()[t].var);
    }
    if (helper->Sizes()[t].var != kNoIntegerVariable) {
      vars->push_back(helper->Sizes()[t].var);
    }
    if (helper->Ends()[t].var != kNoIntegerVariable) {
      vars->push_back(helper->Ends()[t].var);
    }
    if (helper->IsOptional(t) && !helper->IsAbsent(t) &&
        !helper->IsPresent(t)) {
      const Literal l = helper->PresenceLiteral(t);
      if (encoder->GetLiteralView(l) == kNoIntegerVariable &&
          encoder->GetLiteralView(l.Negated()) == kNoIntegerVariable) {
        model->Add(NewIntegerVariableFromLiteral(l));
      }
      const IntegerVariable direct_view = encoder->GetLiteralView(l);
      if (direct_view != kNoIntegerVariable) {
        vars->push_back(direct_view);
      } else {
        vars->push_back(encoder->GetLiteralView(l.Negated()));
        DCHECK_NE(vars->back(), kNoIntegerVariable);
      }
    }
  }
}

}  // namespace

struct EnergyEvent {
  IntegerValue x_start_min;
  IntegerValue x_start_max;
  IntegerValue x_end_min;
  IntegerValue x_end_max;
  AffineExpression x_size;
  IntegerValue y_min = IntegerValue(0);  // Useful for no_overlap_2d.
  IntegerValue y_max = IntegerValue(0);  // Useful for no_overlap_2d.
  AffineExpression y_size;

  // Energy will be present only if the x_size * y_size could be linearized.
  std::optional<LinearExpression> energy;
  LiteralIndex presence_literal_index = kNoLiteralIndex;

  // Caches for MinOf(x_size), MinOf(y_size), and the LP values of the energy
  // and the presence literal.
  IntegerValue x_size_min;
  IntegerValue y_size_min;
  double literal_lp = 1.0;
  double energy_lp = 0.0;

  // Used to minimize the increase on the y axis for rectangles.
  double y_spread = 0.0;

  // The actual value of the presence literal of the interval(s) is checked
  // when the event is created. A value of kNoLiteralIndex indicates that either
  // the interval was not optional, or that its presence literal is true at
  // level zero.
  bool IsPresent() const { return presence_literal_index == kNoLiteralIndex; }

  // Computes the mandatory minimal overlap of the interval with the time window
  // [start, end].
  IntegerValue GetMinOverlap(IntegerValue start, IntegerValue end) const {
    return std::max(std::min({x_end_min - start, end - x_start_max, x_size_min,
                              end - start}),
                    IntegerValue(0));
  }

  std::string DebugString() const {
    return absl::StrCat(
        "EnergyEvent(x_start_min = ", x_start_min.value(),
        ", x_start_max = ", x_start_max.value(),
        ", x_end_min = ", x_end_min.value(),
        ", x_end_max = ", x_end_max.value(),
        ", x_size = ", x_size.DebugString(), ", y_min = ", y_min.value(),
        ", y_max = ", y_max.value(), ", y_size = ", y_size.DebugString(),
        ", energy = ", energy ? energy.value().DebugString() : "{}",
        ", presence_literal_index = ", presence_literal_index.value(), ")");
  }
};

void GenerateCumulativeEnergeticCuts(
    const std::string& cut_name,
    const absl::StrongVector<IntegerVariable, double>& lp_values,
    std::vector<EnergyEvent> events, const AffineExpression capacity,
    std::optional<AffineExpression> makespan, Model* model,
    LinearConstraintManager* manager) {
  // Compute relevant time points.
  // TODO(user): We could reduce this set.
  absl::btree_set<IntegerValue> time_points_set;
  IntegerValue max_end_min = kMinIntegerValue;
  for (const EnergyEvent& event : events) {
    time_points_set.insert(event.x_start_min);
    time_points_set.insert(event.x_start_max);
    time_points_set.insert(event.x_end_min);
    time_points_set.insert(event.x_end_max);
    max_end_min = std::max(max_end_min, event.x_end_min);
  }
  const std::vector<IntegerValue> time_points(time_points_set.begin(),
                                              time_points_set.end());

  double sum_of_energies_lp = 0.0;
  for (const EnergyEvent& event : events) {  // Min contribution is zero.
    sum_of_energies_lp += event.energy_lp;
  }

  // Compute the energetic contribution of a task in a given time window, and
  // add it either to energy_lp, or to the cut.
  // It returns false if it tried to generate the cut, and failed.
  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();

  // Checks the precondition of the code.
  DCHECK(!makespan.has_value() || integer_trail->IsFixed(capacity));

  const auto add_one_event = [integer_trail, &lp_values](
                                 const EnergyEvent& event,
                                 IntegerValue window_start,
                                 IntegerValue window_end,
                                 double* energy_lp = nullptr,
                                 LinearConstraintBuilder* cut = nullptr,
                                 bool* add_energy_to_name = nullptr,
                                 bool* add_quadratic_to_name = nullptr,
                                 bool* add_opt_to_name = nullptr,
                                 bool* add_lifted_to_name = nullptr) {
    if (event.x_end_min <= window_start || event.x_start_max >= window_end) {
      return true;  // Event can move outside the time window.
    }

    const bool compute_energy_only = energy_lp != nullptr;
    if (compute_energy_only) {
      DCHECK(cut == nullptr);
      DCHECK(add_energy_to_name == nullptr);
      DCHECK(add_quadratic_to_name == nullptr);
      DCHECK(add_opt_to_name == nullptr);
      DCHECK(add_lifted_to_name == nullptr);
    } else {
      DCHECK(cut != nullptr);
      DCHECK(add_energy_to_name != nullptr);
      DCHECK(add_quadratic_to_name != nullptr);
      DCHECK(add_opt_to_name != nullptr);
      DCHECK(add_lifted_to_name != nullptr);
    }

    if (event.x_start_min >= window_start && event.x_end_max <= window_end) {
      // Event is always contained by the time window.
      if (event.IsPresent()) {
        if (event.energy) {
          // We favor the energy info instead of the McCormick relaxation.
          if (compute_energy_only) {
            *energy_lp += event.energy.value().LpValue(lp_values);
          } else {
            cut->AddLinearExpression(event.energy.value());
            *add_energy_to_name = true;
          }
        } else {  // The energy was not linearized. Thus it is quadratic.
          if (compute_energy_only) {
            *energy_lp +=
                ToDouble(event.y_size_min) * event.x_size.LpValue(lp_values) +
                ToDouble(event.x_size_min) * event.y_size.LpValue(lp_values) -
                ToDouble(event.x_size_min * event.y_size_min);
          } else {
            cut->AddQuadraticLowerBound(event.x_size, event.y_size,
                                        integer_trail);
            *add_quadratic_to_name = true;
          }
        }
      } else {  // Event is optional.
        const IntegerValue min_energy = std::max(
            event.x_size_min * event.y_size_min,
            event.energy ? event.energy.value().LevelZeroMin(integer_trail)
                         : IntegerValue(0));
        if (compute_energy_only) {
          *energy_lp += event.literal_lp * ToDouble(min_energy);
        } else {
          if (!cut->AddLiteralTerm(Literal(event.presence_literal_index),
                                   min_energy)) {
            return false;
          }

          *add_opt_to_name = true;
          if (min_energy > event.x_size_min * event.y_size_min) {
            *add_energy_to_name = true;
          }
        }
      }
    } else {  // The event has a mandatory overlap with the time window.
      const IntegerValue min_overlap =
          event.GetMinOverlap(window_start, window_end);
      if (min_overlap <= 0) return true;

      if (event.IsPresent()) {
        if (compute_energy_only) {
          *energy_lp += event.y_size.LpValue(lp_values) * ToDouble(min_overlap);
        } else {
          cut->AddTerm(event.y_size, min_overlap);
          *add_lifted_to_name = true;
        }
      } else {
        if (compute_energy_only) {
          *energy_lp +=
              event.literal_lp * ToDouble(min_overlap * event.y_size_min);
        } else {
          if (!cut->AddLiteralTerm(Literal(event.presence_literal_index),
                                   min_overlap * event.y_size_min)) {
            return false;
          }
          *add_opt_to_name = true;
        }
      }
    }
    return true;
  };

  // Currently, we look at all the possible time windows, and will push all cuts
  // in the TopNCuts object. From our observations, this generator creates only
  // a few cuts for a given run.
  //
  // The complexity of this loop is n^3. if we follow the latest research, we
  // could implement this in n log^2(n). Still, this is not visible in the
  // profile as we only this method at the root node,
  struct OverloadedTimeWindow {
    IntegerValue start;
    IntegerValue end;
  };
  std::vector<OverloadedTimeWindow> overloaded_time_windows;
  const double capacity_lp = capacity.LpValue(lp_values);
  const double makespan_lp = makespan.has_value()
                                 ? makespan->LpValue(lp_values)
                                 : std::numeric_limits<double>::infinity();

  const int num_time_points = time_points.size();
  for (int i = 0; i + 1 < num_time_points; ++i) {
    const IntegerValue window_start = time_points[i];
    // After max_end_min, all tasks can fit before window_start.
    if (window_start >= max_end_min) break;

    // if (ToDouble(window_start) >= makespan_lp) continue;
    for (int j = i + 1; j < num_time_points; ++j) {
      const IntegerValue window_end = time_points[j];
      const double max_energy_lp =
          ToDouble(window_end - window_start) * capacity_lp;
      const double energy_up_to_makespan_lp =
          makespan.has_value()
              ? capacity_lp * (makespan_lp - ToDouble(window_start))
              : std::numeric_limits<double>::infinity();

      if (max_energy_lp >= sum_of_energies_lp) break;

      // Scan all events and sum their energetic contributions.
      double energy_lp = 0.0;
      bool energy_correctly_computed = true;
      for (const EnergyEvent& event : events) {
        if (!add_one_event(event, window_start, window_end, &energy_lp)) {
          energy_correctly_computed = false;
          break;  // Abort.
        }
      }
      if (!energy_correctly_computed) continue;

      if (energy_lp >= std::min(max_energy_lp, energy_up_to_makespan_lp) *
                           (1.0 + kMinCutViolation)) {
        overloaded_time_windows.push_back({window_start, window_end});
      }
    }
  }

  if (overloaded_time_windows.empty()) return;

  VLOG(2) << "GenerateCumulativeEnergeticCuts: " << events.size() << " events, "
          << time_points.size() << " time points, "
          << overloaded_time_windows.size() << " overloads detected";

  TopNCuts top_n_cuts(5);
  for (const auto& [window_start, window_end] : overloaded_time_windows) {
    bool cut_generated = true;
    bool add_opt_to_name = false;
    bool add_lifted_to_name = false;
    bool add_quadratic_to_name = false;
    bool add_energy_to_name = false;
    bool use_makespan_in_cut = false;
    LinearConstraintBuilder cut(model, kMinIntegerValue, IntegerValue(0));
    double max_energy_lp = ToDouble(window_end - window_start) * capacity_lp;
    if (makespan.has_value()) {
      const double energy_up_to_makespan_lp =
          capacity_lp * (makespan_lp - ToDouble(window_start));
      if (energy_up_to_makespan_lp < max_energy_lp) {
        max_energy_lp = energy_up_to_makespan_lp;
        use_makespan_in_cut = true;
      }
    }
    if (use_makespan_in_cut) {
      IntegerValue capacity_value = integer_trail->FixedValue(capacity);
      cut.AddConstant(capacity_value * window_start);
      cut.AddTerm(makespan.value(), -capacity_value);
    } else {
      cut.AddTerm(capacity, window_start - window_end);
    }
    for (const EnergyEvent& event : events) {
      if (!add_one_event(event, window_start, window_end,
                         /*energy_lp=*/nullptr, &cut, &add_energy_to_name,
                         &add_quadratic_to_name, &add_opt_to_name,
                         &add_lifted_to_name)) {
        cut_generated = false;
        break;  // Exit the event loop.
      }
    }

    if (cut_generated) {
      std::string full_name = cut_name;
      if (add_opt_to_name) full_name.append("_opt");
      if (add_quadratic_to_name) full_name.append("_quadratic");
      if (add_lifted_to_name) full_name.append("_lifted");
      if (add_energy_to_name) full_name.append("_energy");
      if (use_makespan_in_cut) full_name.append("_makespan");
      top_n_cuts.AddCut(cut.Build(), full_name, lp_values);
    }
  }

  top_n_cuts.TransferToManager(lp_values, manager);
}

void AppendVariablesToCumulativeCut(
    const AffineExpression& capacity,
    const std::vector<AffineExpression>& demands, IntegerTrail* integer_trail,
    CutGenerator* result) {
  for (const AffineExpression& demand_expr : demands) {
    if (!integer_trail->IsFixed(demand_expr)) {
      result->vars.push_back(demand_expr.var);
    }
  }
  if (!integer_trail->IsFixed(capacity)) {
    result->vars.push_back(capacity.var);
  }
}

double ComputeEnergyLp(
    const EnergyEvent& e,
    const absl::StrongVector<IntegerVariable, double>& lp_values,
    IntegerTrail* integer_trail) {
  if (e.presence_literal_index == kNoLiteralIndex) {
    if (e.energy) {
      return e.energy.value().LpValue(lp_values);
    } else {  // demand and size are not fixed.
      // X * Y >= X * y_min + x_min * Y - x_min * y_min.
      return ToDouble(e.y_size_min) * e.x_size.LpValue(lp_values) +
             ToDouble(e.x_size_min) * e.y_size.LpValue(lp_values) -
             ToDouble(e.x_size_min * e.y_size_min);
    }
  } else {
    const IntegerValue min_energy =
        std::max(e.x_size_min * e.y_size_min,
                 e.energy ? e.energy.value().LevelZeroMin(integer_trail)
                          : IntegerValue(0));
    return e.literal_lp * ToDouble(min_energy);
  }
}

CutGenerator CreateCumulativeEnergyCutGenerator(
    SchedulingConstraintHelper* helper, const AffineExpression& capacity,
    const std::vector<AffineExpression>& demands,
    const std::vector<std::optional<LinearExpression>>& energies,
    const std::optional<AffineExpression>& makespan, Model* model) {
  CutGenerator result;

  Trail* trail = model->GetOrCreate<Trail>();
  IntegerEncoder* encoder = model->GetOrCreate<IntegerEncoder>();
  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  AppendVariablesToCumulativeCut(capacity, demands, integer_trail, &result);
  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  for (const auto& energy : energies) {
    if (!energy.has_value()) continue;
    result.vars.insert(result.vars.end(), energy.value().vars.begin(),
                       energy.value().vars.end());
  }
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  result.generate_cuts =
      [makespan, capacity, demands, energies, trail, helper, model, encoder](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();

        std::vector<EnergyEvent> events;
        for (int i = 0; i < helper->NumTasks(); ++i) {
          if (helper->IsAbsent(i)) continue;
          if (integer_trail->LevelZeroUpperBound(demands[i]) == 0 ||
              helper->SizeMin(i) == 0) {
            continue;
          }

          EnergyEvent e;
          e.x_start_min = helper->StartMin(i);
          e.x_start_max = helper->StartMax(i);
          e.x_end_min = helper->EndMin(i);
          e.x_end_max = helper->EndMax(i);
          e.x_size = helper->Sizes()[i];
          e.y_size = demands[i];
          e.energy = energies[i];
          e.x_size_min = helper->SizeMin(i);
          e.y_size_min = integer_trail->LevelZeroLowerBound(demands[i]);
          if (!helper->IsPresent(i)) {
            e.presence_literal_index = helper->PresenceLiteral(i).Index();
            e.literal_lp = GetLiteralLpValue(Literal(e.presence_literal_index),
                                             lp_values, encoder);
          }
          e.energy_lp = ComputeEnergyLp(e, lp_values, integer_trail);
          events.push_back(e);
        }

        GenerateCumulativeEnergeticCuts("CumulativeEnergy", lp_values, events,
                                        capacity, makespan, model, manager);
        return true;
      };

  return result;
}

CutGenerator CreateNoOverlapEnergyCutGenerator(
    SchedulingConstraintHelper* helper,
    const std::optional<AffineExpression>& makespan, Model* model) {
  CutGenerator result;

  Trail* trail = model->GetOrCreate<Trail>();
  IntegerEncoder* encoder = model->GetOrCreate<IntegerEncoder>();

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  // We need to convert AffineExpression to LinearExpression for the energy.
  std::vector<LinearExpression> sizes;
  sizes.reserve(helper->NumTasks());
  for (int i = 0; i < helper->NumTasks(); ++i) {
    LinearConstraintBuilder builder(model);
    builder.AddTerm(helper->Sizes()[i], IntegerValue(1));
    sizes.push_back(builder.BuildExpression());
  }

  result.generate_cuts =
      [makespan, sizes, trail, helper, model, encoder](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        std::vector<EnergyEvent> events;
        for (int i = 0; i < helper->NumTasks(); ++i) {
          if (helper->IsAbsent(i)) continue;
          if (helper->SizeMin(i) == 0) {
            continue;
          }

          EnergyEvent e;
          e.x_start_min = helper->StartMin(i);
          e.x_start_max = helper->StartMax(i);
          e.x_end_min = helper->EndMin(i);
          e.x_end_max = helper->EndMax(i);
          e.x_size = helper->Sizes()[i];
          e.y_size = IntegerValue(1);
          e.energy = sizes[i];
          e.x_size_min = helper->SizeMin(i);
          e.y_size_min = IntegerValue(1);
          if (helper->IsPresent(i)) {
            e.energy_lp = e.energy->LpValue(lp_values);
          } else {
            e.presence_literal_index = helper->PresenceLiteral(i).Index();
            e.literal_lp = GetLiteralLpValue(Literal(e.presence_literal_index),
                                             lp_values, encoder);
            e.energy_lp = e.literal_lp * ToDouble(e.x_size_min);
          }
          events.push_back(e);
        }

        GenerateCumulativeEnergeticCuts("NoOverlapEnergy", lp_values, events,
                                        IntegerValue(1), makespan, model,
                                        manager);
        return true;
      };
  return result;
}

void GenerateNoOverlap2dEnergyCut(
    const std::vector<std::optional<LinearExpression>>& energies,
    absl::Span<int> rectangles, const std::string& cut_name,
    const absl::StrongVector<IntegerVariable, double>& lp_values, Model* model,
    IntegerTrail* integer_trail, IntegerEncoder* encoder,
    LinearConstraintManager* manager, SchedulingConstraintHelper* x_helper,
    SchedulingConstraintHelper* y_helper) {
  std::vector<EnergyEvent> events;
  for (const int rect : rectangles) {
    if (y_helper->SizeMax(rect) == 0 || x_helper->SizeMax(rect) == 0) {
      continue;
    }

    EnergyEvent e;
    e.x_start_min = x_helper->StartMin(rect);
    e.x_start_max = x_helper->StartMax(rect);
    e.x_end_min = x_helper->EndMin(rect);
    e.x_end_max = x_helper->EndMax(rect);
    e.x_size = x_helper->Sizes()[rect];
    e.y_min = y_helper->StartMin(rect);
    e.y_max = y_helper->EndMax(rect);
    e.y_size = y_helper->Sizes()[rect];
    e.energy = energies[rect];
    e.presence_literal_index =
        x_helper->IsPresent(rect)
            ? (y_helper->IsPresent(rect)
                   ? kNoLiteralIndex
                   : y_helper->PresenceLiteral(rect).Index())
            : x_helper->PresenceLiteral(rect).Index();
    if (e.presence_literal_index != kNoLiteralIndex) {
      e.literal_lp = GetLiteralLpValue(Literal(e.presence_literal_index),
                                       lp_values, encoder);
    }
    e.x_size_min = x_helper->SizeMin(rect);
    e.y_size_min = y_helper->SizeMin(rect);
    e.energy_lp = ComputeEnergyLp(e, lp_values, integer_trail);
    events.push_back(e);
  }

  if (events.empty()) return;

  // Compute y_spread.
  double average_d = 0.0;
  for (const auto& e : events) {
    average_d += ToDouble(e.y_min + e.y_max);
  }
  const double average = average_d / 2.0 / static_cast<double>(events.size());
  for (auto& e : events) {
    e.y_spread = std::abs(ToDouble(e.y_max) - average) +
                 std::abs(average - ToDouble(e.y_min));
  }

  TopNCuts top_n_cuts(5);

  std::sort(events.begin(), events.end(),
            [](const EnergyEvent& a, const EnergyEvent& b) {
              return std::tie(a.x_start_min, a.y_spread, a.x_end_max) <
                     std::tie(b.x_start_min, b.y_spread, b.x_end_max);
            });

  // The sum of all energies can be used to stop iterating early.
  double sum_of_all_energies = 0.0;
  for (const auto& e : events) {
    sum_of_all_energies += e.energy_lp;
  }

  CapacityProfile capacity_profile;
  for (int i1 = 0; i1 + 1 < events.size(); ++i1) {
    // For each start time, we will keep the most violated cut generated while
    // scanning the residual intervals.
    int max_violation_end_index = -1;
    double max_relative_violation = 1.0 + kMinCutViolation;
    IntegerValue max_violation_window_start(0);
    IntegerValue max_violation_window_end(0);
    IntegerValue max_violation_y_min(0);
    IntegerValue max_violation_y_max(0);
    IntegerValue max_violation_area(0);
    bool max_violation_use_precise_area = false;

    // Accumulate intervals, areas, energies and check for potential cuts.
    double energy_lp = 0.0;
    IntegerValue window_min = kMaxIntegerValue;
    IntegerValue window_max = kMinIntegerValue;
    IntegerValue y_min = kMaxIntegerValue;
    IntegerValue y_max = kMinIntegerValue;
    capacity_profile.Clear();

    // We sort all tasks (x_start_min(task) >= x_start_min(start_index) by
    // increasing end max.
    std::vector<EnergyEvent> residual_events(events.begin() + i1, events.end());
    std::sort(residual_events.begin(), residual_events.end(),
              [](const EnergyEvent& a, const EnergyEvent& b) {
                return std::tie(a.x_end_max, a.y_spread) <
                       std::tie(b.x_end_max, b.y_spread);
              });
    // Let's process residual tasks and evaluate the violation of the cut at
    // each step. We follow the same structure as the cut creation code below.
    for (int i2 = 0; i2 < residual_events.size(); ++i2) {
      const EnergyEvent& e = residual_events[i2];
      energy_lp += e.energy_lp;
      window_min = std::min(window_min, e.x_start_min);
      window_max = std::max(window_max, e.x_end_max);
      y_min = std::min(y_min, e.y_min);
      y_max = std::max(y_max, e.y_max);
      capacity_profile.AddRectangle(e.x_start_min, e.x_end_max, e.y_min,
                                    e.y_max);

      // Dominance rule. If the next interval also fits in
      // [window_min, window_max]*[y_min, y_max], the cut will be stronger with
      // the next interval/rectangle.
      if (i2 + 1 < residual_events.size() &&
          residual_events[i2 + 1].x_start_min >= window_min &&
          residual_events[i2 + 1].x_end_max <= window_max &&
          residual_events[i2 + 1].y_min >= y_min &&
          residual_events[i2 + 1].y_max <= y_max) {
        continue;
      }

      // Checks the current area vs the sum of all energies.
      // The area is capacity_profile.GetBoundingArea().
      //     We can compare it to the bounding box area:
      //         (window_max - window_min) * (y_max - y_min).
      bool use_precise_area = false;
      IntegerValue precise_area(0);
      double area_lp = 0.0;
      const IntegerValue bbox_area =
          (window_max - window_min) * (y_max - y_min);
      precise_area = capacity_profile.GetBoundingArea();
      use_precise_area = precise_area < bbox_area;
      area_lp = ToDouble(std::min(precise_area, bbox_area));

      if (area_lp >= sum_of_all_energies) {
        break;
      }

      // Compute the violation of the potential cut.
      const double relative_violation = energy_lp / area_lp;
      if (relative_violation > max_relative_violation) {
        max_violation_end_index = i2;
        max_relative_violation = relative_violation;
        max_violation_window_start = window_min;
        max_violation_window_end = window_max;
        max_violation_y_min = y_min;
        max_violation_y_max = y_max;
        max_violation_area = std::min(precise_area, bbox_area);
        max_violation_use_precise_area = use_precise_area;
      }
    }

    if (max_violation_end_index == -1) continue;

    // A maximal violated cut has been found.
    bool cut_generated = true;
    bool add_opt_to_name = false;
    bool add_quadratic_to_name = false;
    bool add_energy_to_name = false;

    // Build the cut.
    LinearConstraintBuilder cut(model, kMinIntegerValue, max_violation_area);

    for (int i2 = 0; i2 <= max_violation_end_index; ++i2) {
      const EnergyEvent& e = residual_events[i2];
      if (e.IsPresent()) {
        if (e.energy) {
          // We favor the energy info instead of the McCormick relaxation.
          cut.AddLinearExpression(e.energy.value());
          add_energy_to_name = true;
        } else {
          cut.AddQuadraticLowerBound(e.x_size, e.y_size, integer_trail);
          add_quadratic_to_name = true;
        }
      } else {
        add_opt_to_name = true;
        const IntegerValue min_energy =
            std::max(e.x_size_min * e.y_size_min,
                     e.energy ? e.energy.value().LevelZeroMin(integer_trail)
                              : IntegerValue(0));
        if (min_energy > e.x_size_min * e.y_size_min) {
          add_energy_to_name = true;
        }
        if (!cut.AddLiteralTerm(Literal(e.presence_literal_index),
                                min_energy)) {
          cut_generated = false;
          break;
        }
      }
    }

    if (cut_generated) {
      std::string full_name = cut_name;
      if (add_opt_to_name) full_name.append("_opt");
      if (add_quadratic_to_name) full_name.append("_quadratic");
      if (add_energy_to_name) full_name.append("_energy");
      if (max_violation_use_precise_area) full_name.append("_precise");
      top_n_cuts.AddCut(cut.Build(), full_name, lp_values);
    }
  }
  top_n_cuts.TransferToManager(lp_values, manager);
}

CutGenerator CreateNoOverlap2dEnergyCutGenerator(
    const std::vector<IntervalVariable>& x_intervals,
    const std::vector<IntervalVariable>& y_intervals, Model* model) {
  CutGenerator result;
  IntervalsRepository* intervals_repository =
      model->GetOrCreate<IntervalsRepository>();
  const int num_rectangles = x_intervals.size();

  std::vector<std::optional<LinearExpression>> energies;
  std::vector<AffineExpression> x_sizes;
  std::vector<AffineExpression> y_sizes;
  for (int i = 0; i < num_rectangles; ++i) {
    x_sizes.push_back(intervals_repository->Size(x_intervals[i]));
    y_sizes.push_back(intervals_repository->Size(y_intervals[i]));
    energies.push_back(
        TryToLinearizeProduct(x_sizes.back(), y_sizes.back(), model));
  }

  SchedulingConstraintHelper* x_helper =
      model->GetOrCreate<IntervalsRepository>()->GetOrCreateHelper(x_intervals);
  SchedulingConstraintHelper* y_helper =
      model->GetOrCreate<IntervalsRepository>()->GetOrCreateHelper(y_intervals);

  AddIntegerVariableFromIntervals(x_helper, model, &result.vars);
  AddIntegerVariableFromIntervals(y_helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();
  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  IntegerEncoder* encoder = model->GetOrCreate<IntegerEncoder>();

  result.generate_cuts =
      [integer_trail, trail, encoder, x_helper, y_helper, model, energies](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;

        if (!x_helper->SynchronizeAndSetTimeDirection(true)) return false;
        if (!y_helper->SynchronizeAndSetTimeDirection(true)) return false;

        const int num_rectangles = x_helper->NumTasks();
        std::vector<int> active_rectangles;
        std::vector<Rectangle> cached_rectangles(num_rectangles);
        for (int rect = 0; rect < num_rectangles; ++rect) {
          if (y_helper->IsAbsent(rect) || y_helper->IsAbsent(rect)) continue;
          // We do not consider rectangles controlled by 2 different unassigned
          // enforcement literals.
          if (!x_helper->IsPresent(rect) && !y_helper->IsPresent(rect) &&
              x_helper->PresenceLiteral(rect) !=
                  y_helper->PresenceLiteral(rect)) {
            continue;
          }

          // TODO(user): It might be possible/better to use some shifted value
          // here, but for now this code is not in the hot spot, so better be
          // defensive and only do connected components on really disjoint
          // rectangles.
          Rectangle& rectangle = cached_rectangles[rect];
          rectangle.x_min = x_helper->StartMin(rect);
          rectangle.x_max = x_helper->EndMax(rect);
          rectangle.y_min = y_helper->StartMin(rect);
          rectangle.y_max = y_helper->EndMax(rect);

          active_rectangles.push_back(rect);
        }

        if (active_rectangles.size() <= 1) return true;

        std::vector<absl::Span<int>> components =
            GetOverlappingRectangleComponents(
                cached_rectangles, absl::MakeSpan(active_rectangles));

        // Forward pass. No need to do a backward pass.
        for (absl::Span<int> rectangles : components) {
          if (rectangles.size() <= 1) continue;

          GenerateNoOverlap2dEnergyCut(
              energies, rectangles, "NoOverlap2dXEnergy", lp_values, model,
              integer_trail, encoder, manager, x_helper, y_helper);
          GenerateNoOverlap2dEnergyCut(
              energies, rectangles, "NoOverlap2dYEnergy", lp_values, model,
              integer_trail, encoder, manager, y_helper, x_helper);
        }

        return true;
      };
  return result;
}

CutGenerator CreateCumulativeTimeTableCutGenerator(
    SchedulingConstraintHelper* helper, const AffineExpression& capacity,
    const std::vector<AffineExpression>& demands, Model* model) {
  CutGenerator result;

  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  AppendVariablesToCumulativeCut(capacity, demands, integer_trail, &result);

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  struct TimeTableEvent {
    int interval_index;
    IntegerValue time;
    bool positive;
    AffineExpression demand;
  };

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [helper, capacity, demands, trail, integer_trail, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        std::vector<TimeTableEvent> events;
        // Iterate through the intervals. If start_max < end_min, the demand
        // is mandatory.
        for (int i = 0; i < helper->NumTasks(); ++i) {
          if (helper->IsAbsent(i)) continue;

          const IntegerValue start_max = helper->StartMax(i);
          const IntegerValue end_min = helper->EndMin(i);

          if (start_max >= end_min) continue;

          TimeTableEvent e1;
          e1.interval_index = i;
          e1.time = start_max;
          e1.demand = demands[i];
          e1.positive = true;

          TimeTableEvent e2 = e1;
          e2.time = end_min;
          e2.positive = false;
          events.push_back(e1);
          events.push_back(e2);
        }

        // Sort events by time.
        // It is also important that all positive event with the same time as
        // negative events appear after for the correctness of the algo below.
        std::sort(events.begin(), events.end(),
                  [](const TimeTableEvent& i, const TimeTableEvent& j) {
                    if (i.time == j.time) {
                      if (i.positive == j.positive) {
                        return i.interval_index < j.interval_index;
                      }
                      return !i.positive;
                    }
                    return i.time < j.time;
                  });

        std::vector<TimeTableEvent> cut_events;
        bool added_positive_event = false;
        for (const TimeTableEvent& e : events) {
          if (e.positive) {
            added_positive_event = true;
            cut_events.push_back(e);
            continue;
          }
          if (added_positive_event && cut_events.size() > 1) {
            // Create cut.
            bool cut_generated = true;
            LinearConstraintBuilder cut(model, kMinIntegerValue,
                                        IntegerValue(0));
            cut.AddTerm(capacity, IntegerValue(-1));
            for (const TimeTableEvent& cut_event : cut_events) {
              if (helper->IsPresent(cut_event.interval_index)) {
                cut.AddTerm(cut_event.demand, IntegerValue(1));
              } else {
                cut_generated &= cut.AddLiteralTerm(
                    helper->PresenceLiteral(cut_event.interval_index),
                    integer_trail->LowerBound(cut_event.demand));
                if (!cut_generated) break;
              }
            }
            if (cut_generated) {
              // Violation of the cut is checked by AddCut so we don't check
              // it here.
              manager->AddCut(cut.Build(), "CumulativeTimeTable", lp_values);
            }
          }
          // Remove the event.
          int new_size = 0;
          for (int i = 0; i < cut_events.size(); ++i) {
            if (cut_events[i].interval_index == e.interval_index) {
              continue;
            }
            cut_events[new_size] = cut_events[i];
            new_size++;
          }
          cut_events.resize(new_size);
          added_positive_event = false;
        }
        return true;
      };
  return result;
}

// Cached Information about one interval.
// Note that everything must correspond to level zero bounds, otherwise the
// generated cut are not valid.

struct CachedIntervalData {
  IntegerValue start_min;
  IntegerValue start_max;
  AffineExpression start;
  IntegerValue end_min;
  IntegerValue end_max;
  AffineExpression end;
  IntegerValue demand_min;
  IntegerValue duration_min;
};

void GenerateCutsBetweenPairOfNonOverlappingTasks(
    const std::string& cut_name,
    const absl::StrongVector<IntegerVariable, double>& lp_values,
    std::vector<CachedIntervalData> events, IntegerValue capacity_max,
    Model* model, LinearConstraintManager* manager) {
  TopNCuts top_n_cuts(5);
  const int num_events = events.size();
  if (num_events <= 1) return;

  std::sort(events.begin(), events.end(),
            [](const CachedIntervalData& e1, const CachedIntervalData& e2) {
              return e1.start_min < e2.start_min ||
                     (e1.start_min == e2.start_min && e1.end_max < e2.end_max);
            });

  // Balas disjunctive cuts on 2 tasks a and b:
  //   start_1 * (duration_1 + start_min_1 - start_min_2) +
  //   start_2 * (duration_2 + start_min_2 - start_min_1) >=
  //       duration_1 * duration_2 +
  //       start_min_1 * duration_2 +
  //       start_min_2 * duration_1
  // From: David L. Applegate, William J. Cook:
  //   A Computational Study of the Job-Shop Scheduling Problem. 149-156
  //   INFORMS Journal on Computing, Volume 3, Number 1, Winter 1991
  const auto add_balas_disjunctive_cut =
      [&](const std::string& local_cut_name, IntegerValue start_min_1,
          IntegerValue duration_min_1, AffineExpression start_1,
          IntegerValue start_min_2, IntegerValue duration_min_2,
          AffineExpression start_2) {
        // Checks hypothesis from the cut.
        if (start_min_2 >= start_min_1 + duration_min_1 ||
            start_min_1 >= start_min_2 + duration_min_2) {
          return;
        }
        const IntegerValue coeff_1 = duration_min_1 + start_min_1 - start_min_2;
        const IntegerValue coeff_2 = duration_min_2 + start_min_2 - start_min_1;
        const IntegerValue rhs = duration_min_1 * duration_min_2 +
                                 duration_min_1 * start_min_2 +
                                 duration_min_2 * start_min_1;

        if (ToDouble(coeff_1) * start_1.LpValue(lp_values) +
                ToDouble(coeff_2) * start_2.LpValue(lp_values) <=
            ToDouble(rhs) - kMinCutViolation) {
          LinearConstraintBuilder cut(model, rhs, kMaxIntegerValue);
          cut.AddTerm(start_1, coeff_1);
          cut.AddTerm(start_2, coeff_2);
          top_n_cuts.AddCut(cut.Build(), local_cut_name, lp_values);
        }
      };

  for (int i = 0; i + 1 < num_events; ++i) {
    const CachedIntervalData& e1 = events[i];
    for (int j = i + 1; j < num_events; ++j) {
      const CachedIntervalData& e2 = events[j];
      if (e2.start_min >= e1.end_max) break;  // Break out of the index2 loop.

      // Encode only the interesting pairs.
      if (e1.demand_min + e2.demand_min <= capacity_max) continue;

      const bool interval_1_can_precede_2 = e1.end_min <= e2.start_max;
      const bool interval_2_can_precede_1 = e2.end_min <= e1.start_max;

      if (interval_1_can_precede_2 && !interval_2_can_precede_1 &&
          e1.end.LpValue(lp_values) >=
              e2.start.LpValue(lp_values) + kMinCutViolation) {
        // interval1.end <= interval2.start
        LinearConstraintBuilder cut(model, kMinIntegerValue, IntegerValue(0));
        cut.AddTerm(e1.end, IntegerValue(1));
        cut.AddTerm(e2.start, IntegerValue(-1));
        top_n_cuts.AddCut(cut.Build(),
                          absl::StrCat(cut_name, "DetectedPrecedence"),
                          lp_values);
      } else if (interval_2_can_precede_1 && !interval_1_can_precede_2 &&
                 e2.end.LpValue(lp_values) >=
                     e1.start.LpValue(lp_values) + kMinCutViolation) {
        // interval2.end <= interval1.start
        LinearConstraintBuilder cut(model, kMinIntegerValue, IntegerValue(0));
        cut.AddTerm(e2.end, IntegerValue(1));
        cut.AddTerm(e1.start, IntegerValue(-1));
        top_n_cuts.AddCut(cut.Build(),
                          absl::StrCat(cut_name, "DetectedPrecedence"),
                          lp_values);
      } else {
        add_balas_disjunctive_cut(absl::StrCat(cut_name, "DisjunctionOnStart"),
                                  e1.start_min, e1.duration_min, e1.start,
                                  e2.start_min, e2.duration_min, e2.start);
        add_balas_disjunctive_cut(absl::StrCat(cut_name, "DisjunctionOnEnd"),
                                  -e1.end_max, e1.duration_min,
                                  e1.end.Negated(), -e2.end_max,
                                  e2.duration_min, e2.end.Negated());
      }
    }
  }

  top_n_cuts.TransferToManager(lp_values, manager);
}

CutGenerator CreateCumulativePrecedenceCutGenerator(
    SchedulingConstraintHelper* helper, const AffineExpression& capacity,
    const std::vector<AffineExpression>& demands, Model* model) {
  CutGenerator result;

  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  AppendVariablesToCumulativeCut(capacity, demands, integer_trail, &result);

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [trail, integer_trail, helper, demands, capacity, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        std::vector<CachedIntervalData> events;
        for (int t = 0; t < helper->NumTasks(); ++t) {
          if (!helper->IsPresent(t)) continue;
          CachedIntervalData event;
          event.start_min = helper->StartMin(t);
          event.start_max = helper->StartMax(t);
          event.start = helper->Starts()[t];
          event.end_min = helper->EndMin(t);
          event.end_max = helper->EndMax(t);
          event.end = helper->Ends()[t];
          event.demand_min = integer_trail->LowerBound(demands[t]);
          event.duration_min = helper->SizeMin(t);
          events.push_back(event);
        }

        const IntegerValue capacity_max = integer_trail->UpperBound(capacity);
        GenerateCutsBetweenPairOfNonOverlappingTasks(
            "Cumulative", lp_values, std::move(events), capacity_max, model,
            manager);
        return true;
      };
  return result;
}

CutGenerator CreateNoOverlapPrecedenceCutGenerator(
    SchedulingConstraintHelper* helper, Model* model) {
  CutGenerator result;

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [trail, helper, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        std::vector<CachedIntervalData> events;
        for (int t = 0; t < helper->NumTasks(); ++t) {
          if (!helper->IsPresent(t)) continue;
          CachedIntervalData event;
          event.start_min = helper->StartMin(t);
          event.start_max = helper->StartMax(t);
          event.start = helper->Starts()[t];
          event.end_min = helper->EndMin(t);
          event.end_max = helper->EndMax(t);
          event.end = helper->Ends()[t];
          event.demand_min = IntegerValue(1);
          event.duration_min = helper->SizeMin(t);
          events.push_back(event);
        }

        GenerateCutsBetweenPairOfNonOverlappingTasks(
            "NoOverlap", lp_values, std::move(events), IntegerValue(1), model,
            manager);
        return true;
      };

  return result;
}

// Stores the event for a rectangle along the two axis x and y.
//   For a no_overlap constraint, y is always of size 1 between 0 and 1.
//   For a cumulative constraint, y is the demand that must be between 0 and
//       capacity_max.
//   For a no_overlap_2d constraint, y the other dimension of the rect.
struct CtEvent {
  // The start min of the x interval.
  IntegerValue x_start_min;

  // The size min of the x interval.
  IntegerValue x_size_min;

  // The end of the x interval.
  AffineExpression x_end;

  // The lp value of the end of the x interval.
  double x_lp_end;

  // The start min of the y interval.
  IntegerValue y_start_min;

  // The end max of the y interval.
  IntegerValue y_end_max;

  // The min energy of the task (this is always larger or equal to x_size_min *
  // y_size_min).
  IntegerValue energy_min;

  // Indicates if the events used the optional energy information from the
  // model.
  bool use_energy = false;

  // Indicates if the cut is lifted, that is if it includes tasks that are not
  // strictly contained in the current time window.
  bool lifted = false;

  // If we know that the size on y is fixed, we can use some heuristic to
  // compute the maximum subset sums under the capacity and use that instead
  // of the full capacity. If any of the considered event have this at -1, we
  // will not use this.
  IntegerValue fixed_y_size = -1;

  std::string DebugString() const {
    return absl::StrCat("CtEvent(x_end = ", x_end.DebugString(),
                        ", x_start_min = ", x_start_min.value(),
                        ", x_size_min = ", x_size_min.value(),
                        ", x_lp_end = ", x_lp_end,
                        ", y_start_min = ", y_start_min.value(),
                        ", y_end_max = ", y_end_max.value(),
                        ", energy_min = ", energy_min.value(),
                        ", use_energy = ", use_energy, ", lifted = ", lifted);
  }
};

// We generate the cut from the Smith's rule from:
// M. Queyranne, Structure of a simple scheduling polyhedron,
// Mathematical Programming 58 (1993), 263–285
//
// The original cut is:
//    sum(end_min_i * duration_min_i) >=
//        (sum(duration_min_i^2) + sum(duration_min_i)^2) / 2
// We strenghten this cuts by noticing that if all tasks starts after S,
// then replacing end_min_i by (end_min_i - S) is still valid.
//
// A second difference is that we look at a set of intervals starting
// after a given start_min, sorted by relative (end_lp - start_min).
void GenerateCompletionTimeCuts(
    const std::string& cut_name,
    const absl::StrongVector<IntegerVariable, double>& lp_values,
    std::vector<CtEvent> events, bool use_lifting, Model* model,
    LinearConstraintManager* manager) {
  TopNCuts top_n_cuts(5);

  // Sort by start min to bucketize by start_min.
  std::sort(events.begin(), events.end(),
            [](const CtEvent& e1, const CtEvent& e2) {
              return e1.x_start_min < e2.x_start_min;
            });
  for (int start = 0; start + 1 < events.size(); ++start) {
    // Skip to the next start_min value.
    if (start > 0 &&
        events[start].x_start_min == events[start - 1].x_start_min) {
      continue;
    }

    const IntegerValue sequence_start_min = events[start].x_start_min;
    std::vector<CtEvent> residual_tasks(events.begin() + start, events.end());

    // We look at event that start before sequence_start_min, but are forced
    // to cross this time point. In that case, we replace this event by a
    // truncated event starting at sequence_start_min. To do this, we reduce
    // the size_min, align the start_min with the sequence_start_min, and
    // scale the energy down accordingly.
    if (use_lifting) {
      for (int before = 0; before < start; ++before) {
        if (events[before].x_start_min + events[before].x_size_min >
            sequence_start_min) {
          // Build the vector of energies as the vector of sizes.
          CtEvent event = events[before];  // Copy.
          event.lifted = true;
          const IntegerValue old_size_min = event.x_size_min;
          event.x_size_min =
              event.x_size_min + event.x_start_min - sequence_start_min;
          event.x_start_min = sequence_start_min;
          // We can rescale the energy min correctly.
          //
          // Let's take the example of a rectangle of size 2 * 20 that overlaps
          // sequence start min by 1, and that can rotate by 90 degrees.
          // The energy min is 40, size min is 2, size_max is 20.
          // If the rectangle is horizontal, the lifted energy is:
          //     (20 - 1) * 2 = 38
          // If the rectangle is vertical, the lifted energy is:
          //     (2 - 1) * 20 = 20
          // The min of the two is always reached when size = size_min.
          event.energy_min = event.energy_min * event.x_size_min / old_size_min;
          residual_tasks.push_back(event);
        }
      }
    }

    std::sort(residual_tasks.begin(), residual_tasks.end(),
              [](const CtEvent& e1, const CtEvent& e2) {
                return e1.x_lp_end < e2.x_lp_end;
              });

    int best_end = -1;
    double best_efficacy = 0.01;
    IntegerValue best_min_contrib(0);
    IntegerValue sum_duration(0);
    IntegerValue sum_square_duration(0);
    IntegerValue best_capacity(0);
    double unscaled_lp_contrib = 0.0;
    IntegerValue current_start_min(kMaxIntegerValue);
    IntegerValue y_start_min = kMaxIntegerValue;
    IntegerValue y_end_max = kMinIntegerValue;

    bool use_dp = true;
    MaxBoundedSubsetSum dp(0);
    for (int i = 0; i < residual_tasks.size(); ++i) {
      const CtEvent& event = residual_tasks[i];
      DCHECK_GE(event.x_start_min, sequence_start_min);
      const IntegerValue energy = event.energy_min;
      sum_duration += energy;
      sum_square_duration += energy * energy;
      unscaled_lp_contrib += event.x_lp_end * ToDouble(energy);
      current_start_min = std::min(current_start_min, event.x_start_min);

      // For the capacity, we use the worse |y_max - y_min| and if all the tasks
      // so far have a fixed demand with a gcd > 1, we can round it down.
      //
      // TODO(user): Use dynamic programming to compute all possible values for
      // the sum of demands as long as the involved numbers are small or the
      // number of tasks are small.
      y_start_min = std::min(y_start_min, event.y_start_min);
      y_end_max = std::max(y_end_max, event.y_end_max);
      if (event.fixed_y_size < 0) use_dp = false;
      if (use_dp) {
        if (i == 0) {
          dp.Reset((y_end_max - y_start_min).value());
        } else {
          if (y_end_max - y_start_min != dp.Bound()) {
            use_dp = false;
          }
        }
      }
      if (use_dp) {
        dp.Add(event.fixed_y_size.value());
      }

      const IntegerValue capacity =
          use_dp ? IntegerValue(dp.CurrentMax()) : y_end_max - y_start_min;

      // We compute the cuts like if it was a disjunctive cut with all the
      // duration actually equal to energy / capacity. But to keep the
      // computation in the integer domain, we multiply by capacity
      // everywhere instead.
      const IntegerValue min_contrib =
          (sum_duration * sum_duration + sum_square_duration) / 2 +
          current_start_min * sum_duration * capacity;

      // We compute the efficacity in the unscaled domain where the l2 norm of
      // the cuts is exactly the sqrt of  the sum of squared duration.
      const double efficacy =
          (ToDouble(min_contrib) / ToDouble(capacity) - unscaled_lp_contrib) /
          std::sqrt(ToDouble(sum_square_duration));

      // TODO(user): Check overflow and ignore if too big.
      if (efficacy > best_efficacy) {
        best_efficacy = efficacy;
        best_end = i;
        best_min_contrib = min_contrib;
        best_capacity = capacity;
      }
    }
    if (best_end != -1) {
      LinearConstraintBuilder cut(model, best_min_contrib, kMaxIntegerValue);
      bool is_lifted = false;
      bool add_energy_to_name = false;
      for (int i = 0; i <= best_end; ++i) {
        const CtEvent& event = residual_tasks[i];
        is_lifted |= event.lifted;
        add_energy_to_name |= event.use_energy;
        cut.AddTerm(event.x_end, event.energy_min * best_capacity);
      }
      std::string full_name = cut_name;
      if (is_lifted) full_name.append("_lifted");
      if (add_energy_to_name) full_name.append("_energy");
      top_n_cuts.AddCut(cut.Build(), full_name, lp_values);
    }
  }
  top_n_cuts.TransferToManager(lp_values, manager);
}

CutGenerator CreateNoOverlapCompletionTimeCutGenerator(
    SchedulingConstraintHelper* helper, Model* model) {
  CutGenerator result;

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [trail, helper, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        auto generate_cuts = [&lp_values, model, manager,
                              helper](const std::string& cut_name) {
          std::vector<CtEvent> events;
          for (int index = 0; index < helper->NumTasks(); ++index) {
            if (!helper->IsPresent(index)) continue;
            const IntegerValue size_min = helper->SizeMin(index);
            if (size_min > 0) {
              const AffineExpression end_expr = helper->Ends()[index];
              CtEvent event;
              event.x_start_min = helper->StartMin(index);
              event.x_size_min = size_min;
              event.x_end = end_expr;
              event.x_lp_end = end_expr.LpValue(lp_values);
              event.y_start_min = IntegerValue(0);
              event.y_end_max = IntegerValue(1);
              event.energy_min = size_min;
              events.push_back(event);
            }
          }
          GenerateCompletionTimeCuts(cut_name, lp_values, std::move(events),
                                     /*use_lifting=*/false, model, manager);
        };
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;
        generate_cuts("NoOverlapCompletionTime");
        if (!helper->SynchronizeAndSetTimeDirection(false)) return false;
        generate_cuts("NoOverlapCompletionTimeMirror");
        return true;
      };
  return result;
}

CutGenerator CreateCumulativeCompletionTimeCutGenerator(
    SchedulingConstraintHelper* helper, const AffineExpression& capacity,
    const std::vector<AffineExpression>& demands,
    const std::vector<std::optional<LinearExpression>>& energies,
    Model* model) {
  CutGenerator result;

  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  AppendVariablesToCumulativeCut(capacity, demands, integer_trail, &result);

  AddIntegerVariableFromIntervals(helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [trail, integer_trail, helper, demands, energies, capacity, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;

        const IntegerValue capacity_max = integer_trail->UpperBound(capacity);
        auto generate_cuts = [&lp_values, model, manager, helper, capacity_max,
                              integer_trail, &demands,
                              &energies](const std::string& cut_name) {
          std::vector<CtEvent> events;
          for (int index = 0; index < helper->NumTasks(); ++index) {
            if (!helper->IsPresent(index)) continue;
            if (helper->SizeMin(index) > 0 &&
                integer_trail->LowerBound(demands[index]) > 0) {
              const AffineExpression end_expr = helper->Ends()[index];
              const IntegerValue size_min = helper->SizeMin(index);
              const IntegerValue demand_min =
                  integer_trail->LowerBound(demands[index]);

              CtEvent event;
              event.x_start_min = helper->StartMin(index);
              event.x_size_min = size_min;
              event.x_end = end_expr;
              event.x_lp_end = end_expr.LpValue(lp_values);
              event.y_start_min = IntegerValue(0);
              event.y_end_max = IntegerValue(capacity_max);
              event.energy_min = size_min * demand_min;
              if (integer_trail->IsFixed(demands[index])) {
                event.fixed_y_size = demand_min;
              }

              // TODO(user): Investigate and re-enable a correct version.
              if (/*DISABLES_CODE*/ (false) && energies[index].has_value()) {
                const IntegerValue linearized_energy =
                    LinExprLowerBound(energies[index].value(), *integer_trail);
                if (linearized_energy > event.energy_min) {
                  event.energy_min = linearized_energy;
                  event.use_energy = true;
                }
              }
              events.push_back(event);
            }
          }
          GenerateCompletionTimeCuts(cut_name, lp_values, std::move(events),
                                     /*use_lifting=*/true, model, manager);
        };
        if (!helper->SynchronizeAndSetTimeDirection(true)) return false;
        generate_cuts("CumulativeCompletionTime");
        if (!helper->SynchronizeAndSetTimeDirection(false)) return false;
        generate_cuts("CumulativeCompletionTimeMirror");
        return true;
      };
  return result;
}

CutGenerator CreateNoOverlap2dCompletionTimeCutGenerator(
    const std::vector<IntervalVariable>& x_intervals,
    const std::vector<IntervalVariable>& y_intervals, Model* model) {
  CutGenerator result;

  SchedulingConstraintHelper* x_helper =
      model->GetOrCreate<IntervalsRepository>()->GetOrCreateHelper(x_intervals);
  SchedulingConstraintHelper* y_helper =
      model->GetOrCreate<IntervalsRepository>()->GetOrCreateHelper(y_intervals);

  AddIntegerVariableFromIntervals(x_helper, model, &result.vars);
  AddIntegerVariableFromIntervals(y_helper, model, &result.vars);
  gtl::STLSortAndRemoveDuplicates(&result.vars);

  Trail* trail = model->GetOrCreate<Trail>();

  result.generate_cuts =
      [trail, x_helper, y_helper, model](
          const absl::StrongVector<IntegerVariable, double>& lp_values,
          LinearConstraintManager* manager) {
        if (trail->CurrentDecisionLevel() > 0) return true;

        if (!x_helper->SynchronizeAndSetTimeDirection(true)) return false;
        if (!y_helper->SynchronizeAndSetTimeDirection(true)) return false;

        const int num_rectangles = x_helper->NumTasks();
        std::vector<int> active_rectangles;
        std::vector<IntegerValue> cached_areas(num_rectangles);
        std::vector<Rectangle> cached_rectangles(num_rectangles);
        for (int rect = 0; rect < num_rectangles; ++rect) {
          if (!y_helper->IsPresent(rect) || !y_helper->IsPresent(rect))
            continue;

          cached_areas[rect] =
              x_helper->SizeMin(rect) * y_helper->SizeMin(rect);
          if (cached_areas[rect] == 0) continue;

          // TODO(user): It might be possible/better to use some shifted value
          // here, but for now this code is not in the hot spot, so better be
          // defensive and only do connected components on really disjoint
          // rectangles.
          Rectangle& rectangle = cached_rectangles[rect];
          rectangle.x_min = x_helper->StartMin(rect);
          rectangle.x_max = x_helper->EndMax(rect);
          rectangle.y_min = y_helper->StartMin(rect);
          rectangle.y_max = y_helper->EndMax(rect);

          active_rectangles.push_back(rect);
        }

        if (active_rectangles.size() <= 1) return true;

        std::vector<absl::Span<int>> components =
            GetOverlappingRectangleComponents(
                cached_rectangles, absl::MakeSpan(active_rectangles));
        for (absl::Span<int> rectangles : components) {
          if (rectangles.size() <= 1) continue;

          auto generate_cuts = [&lp_values, model, manager, &rectangles,
                                &cached_areas](
                                   const std::string& cut_name,
                                   SchedulingConstraintHelper* x_helper,
                                   SchedulingConstraintHelper* y_helper) {
            std::vector<CtEvent> events;

            for (const int rect : rectangles) {
              const AffineExpression x_end_expr = x_helper->Ends()[rect];
              CtEvent event;
              event.x_start_min = x_helper->ShiftedStartMin(rect);
              event.x_size_min = x_helper->SizeMin(rect);
              event.x_end = x_end_expr;
              event.x_lp_end = x_end_expr.LpValue(lp_values);
              event.y_start_min = y_helper->ShiftedStartMin(rect);
              event.y_end_max = y_helper->ShiftedEndMax(rect);
              event.energy_min =
                  x_helper->SizeMin(rect) * y_helper->SizeMin(rect);
              events.push_back(event);
            }

            GenerateCompletionTimeCuts(cut_name, lp_values, std::move(events),
                                       /*use_lifting=*/true, model, manager);
          };

          if (!x_helper->SynchronizeAndSetTimeDirection(true)) return false;
          if (!y_helper->SynchronizeAndSetTimeDirection(true)) return false;
          generate_cuts("NoOverlap2dXCompletionTime", x_helper, y_helper);
          generate_cuts("NoOverlap2dYCompletionTime", y_helper, x_helper);
          if (!x_helper->SynchronizeAndSetTimeDirection(false)) return false;
          if (!y_helper->SynchronizeAndSetTimeDirection(false)) return false;
          generate_cuts("NoOverlap2dXCompletionTimeMirror", x_helper, y_helper);
          generate_cuts("NoOverlap2dYCompletionTimeMirror", y_helper, x_helper);
        }
        return true;
      };
  return result;
}

}  // namespace sat
}  // namespace operations_research
