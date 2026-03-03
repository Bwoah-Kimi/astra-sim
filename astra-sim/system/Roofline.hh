/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __ROOFLINE_HH__
#define __ROOFLINE_HH__

#include <cstdint>

namespace AstraSim {

  class Roofline {
  public:
    Roofline(double peak_perf);
    Roofline(double bandwidth, double peak_perf);
    void set_bandwidth(double bandwidth);
    double get_perf(double operational_intensity);
    double get_perf_with_stats(double operational_intensity, uint64_t num_ops);
    void print_roofline_stats();
    void update_stats(double operational_intensity, uint64_t num_ops);
    void reset_stats();

    // SCALE-Sim statistics
    void update_scalesim_stats(
      double mapping_efficiency,
      double compute_utilization,
      double overall_utilization,
      uint64_t num_ops
    );
    void print_scalesim_stats();

    // Public access methods - Roofline
    double get_peak_perf() const;
    double get_bandwidth() const;
    uint64_t get_total_ops() const;
    double get_weighted_avg_oi() const;
    double get_bandwidth_limited_ratio() const;
    double get_compute_limited_ratio() const;
    uint64_t get_total_calls() const;

    // Public access methods - SCALE-Sim
    uint64_t get_scalesim_total_ops() const;
    uint64_t get_scalesim_total_matmuls() const;
    double get_scalesim_weighted_avg_mapping_eff() const;
    double get_scalesim_weighted_avg_compute_util() const;
    double get_scalesim_weighted_avg_overall_util() const;

  private:
    double bandwidth;
    double peak_perf;

    // Roofline statistical information
    uint64_t total_ops;                    // Total operations (FLOPS)
    double weighted_oi_sum;                // OI weighted sum
    uint64_t bandwidth_limited_ops;        // Bandwidth-limited operations
    uint64_t compute_limited_ops;          // Compute-limited operations
    uint64_t total_calls;                  // Total calls

    // SCALE-Sim statistical information
    uint64_t scalesim_total_ops;               // Total operations from SCALE-Sim MatMuls
    uint64_t scalesim_total_matmuls;          // Total MatMul calls in SCALE
    double scalesim_weighted_mapping_eff_sum; // Weighted sum of mapping efficiency
    double scalesim_weighted_compute_util_sum; // Weighted sum of compute utilization
    double scalesim_weighted_overall_util_sum; // Weighted sum of overall utilization
  };

}  // namespace AstraSim

#endif /* __ROOFLINE_HH__ */
