/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/system/Roofline.hh"

#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace AstraSim;

Roofline::Roofline(double peak_perf) : peak_perf(peak_perf) {
    reset_stats();
}

Roofline::Roofline(double bandwidth, double peak_perf)
    : bandwidth(bandwidth),
    peak_perf(peak_perf) {
    reset_stats();
}

void Roofline::set_bandwidth(double bandwidth) {
    this->bandwidth = bandwidth;
}

double Roofline::get_perf(double operational_intensity) {
    return min(bandwidth * operational_intensity, peak_perf);
}


void Roofline::update_stats(double operational_intensity, uint64_t num_ops) {
    double bandwidth_limited_perf = bandwidth * operational_intensity;

    // Collect statistics
    total_ops += num_ops;
    weighted_oi_sum += operational_intensity * num_ops;
    total_calls++;

    // Determine if the operation is bandwidth-limited or compute-limited
    if (bandwidth_limited_perf <= peak_perf) {
        bandwidth_limited_ops += num_ops;
    }
    else {
        compute_limited_ops += num_ops;
    }
}

double Roofline::get_perf_with_stats(double operational_intensity, uint64_t num_ops) {
    double bandwidth_limited_perf = bandwidth * operational_intensity;
    double effective_perf = min(bandwidth_limited_perf, peak_perf);

    update_stats(operational_intensity, num_ops);

    return effective_perf;
}

void Roofline::update_scalesim_stats(double mapping_efficiency, double compute_utilization, double overall_utilization, uint64_t num_ops) {
    scalesim_total_ops += num_ops;
    scalesim_total_matmuls += 1;
    scalesim_weighted_mapping_eff_sum += mapping_efficiency * num_ops;
    scalesim_weighted_compute_util_sum += compute_utilization * num_ops;
    scalesim_weighted_overall_util_sum += overall_utilization * num_ops;
}

void Roofline::print_roofline_stats() {
    if (total_calls == 0) {
        cout << "[Roofline Stats] No computation operations recorded." << endl;
        return;
    }

    double weighted_avg_oi = weighted_oi_sum / total_ops;
    double bandwidth_limited_ratio = static_cast<double>(bandwidth_limited_ops) / total_ops;
    double compute_limited_ratio = static_cast<double>(compute_limited_ops) / total_ops;

    cout << fixed << setprecision(4);
    cout << "=== Roofline Model Statistics ===" << endl;
    cout << "Peak Performance: " << peak_perf / 1e12 << " TFLOPS" << endl;
    cout << "Memory Bandwidth: " << bandwidth / 1e9 << " GB/s" << endl;
    cout << "Total Operations: " << total_ops << " FLOPS" << endl;
    cout << "Total Calls: " << total_calls << endl;
    cout << "Weighted Average OI: " << weighted_avg_oi << " FLOPS/Byte" << endl;
    cout << "Bandwidth Limited Ops: " << bandwidth_limited_ratio * 100 << "%" << endl;
    cout << "Compute Limited Ops: " << compute_limited_ratio * 100 << "%" << endl;

    double avg_bandwidth_perf = bandwidth * weighted_avg_oi;
    double ratio = avg_bandwidth_perf / peak_perf;
    cout << "Bandwidth Perf / Peak Perf Ratio: " << ratio << endl;

    if (ratio < 1.0) {
        cout << "Overall Bottleneck: MEMORY BANDWIDTH (ratio=" << ratio << ")" << endl;
    }
    else {
        cout << "Overall Bottleneck: COMPUTE CAPACITY (ratio=" << ratio << ")" << endl;
    }
    cout << "=================================" << endl;
}

void Roofline::print_scalesim_stats() {
    if (scalesim_total_matmuls == 0) {
        cout << "[SCALE-Sim Stats] No matmul operations recorded." << endl;
        return;
    }

    double weighted_avg_mapping_eff = scalesim_weighted_mapping_eff_sum / scalesim_total_ops;
    double weighted_avg_compute_util = scalesim_weighted_compute_util_sum / scalesim_total_ops;
    double weighted_avg_overall_util = scalesim_weighted_overall_util_sum / scalesim_total_ops;

    cout << fixed << setprecision(4);
    cout << "=== SCALE-Sim Statistics ===" << endl;
    cout << "Total MatMul Operations: " << scalesim_total_matmuls << endl;
    cout << "Total SCALE-Sim Operations: " << scalesim_total_ops << " FLOPS" << endl;
    cout << "Weighted Average Mapping Efficiency: " << weighted_avg_mapping_eff * 100 << "%" << endl;
    cout << "Weighted Average Compute Utilization: " << weighted_avg_compute_util * 100 << "%" << endl;
    cout << "Weighted Average Overall Utilization: " << weighted_avg_overall_util * 100 << "%" << endl;

    // Identify primary bottleneck
    if (weighted_avg_mapping_eff < weighted_avg_compute_util) {
        cout << "Primary Bottleneck: MAPPING EFFICIENCY (dataflow/tiling)" << endl;
    }
    else {
        cout << "Primary Bottleneck: COMPUTE UTILIZATION (array underutilization)" << endl;
    }
    cout << "============================" << endl;
}

void Roofline::reset_stats() {
    // Reset Roofline statistics
    total_ops = 0;
    weighted_oi_sum = 0.0;
    bandwidth_limited_ops = 0;
    compute_limited_ops = 0;
    total_calls = 0;

    // Reset SCALE-Sim statistics
    scalesim_total_ops = 0;
    scalesim_total_matmuls = 0;
    scalesim_weighted_mapping_eff_sum = 0.0;
    scalesim_weighted_compute_util_sum = 0.0;
    scalesim_weighted_overall_util_sum = 0.0;
}

// Roofline getters
double Roofline::get_peak_perf() const {
    return peak_perf;
}

double Roofline::get_bandwidth() const {
    return bandwidth;
}

uint64_t Roofline::get_total_ops() const {
    return total_ops;
}

double Roofline::get_weighted_avg_oi() const {
    return total_ops > 0 ? weighted_oi_sum / total_ops : 0.0;
}

double Roofline::get_bandwidth_limited_ratio() const {
    return total_ops > 0 ? static_cast<double>(bandwidth_limited_ops) / total_ops : 0.0;
}

double Roofline::get_compute_limited_ratio() const {
    return total_ops > 0 ? static_cast<double>(compute_limited_ops) / total_ops : 0.0;
}

uint64_t Roofline::get_total_calls() const {
    return total_calls;
}

// SCALE-Sim getters
uint64_t Roofline::get_scalesim_total_ops() const {
    return scalesim_total_ops;
}

uint64_t Roofline::get_scalesim_total_matmuls() const {
    return scalesim_total_matmuls;
}

double Roofline::get_scalesim_weighted_avg_mapping_eff() const {
    return scalesim_total_ops > 0 ? scalesim_weighted_mapping_eff_sum / scalesim_total_ops : 0.0;
}

double Roofline::get_scalesim_weighted_avg_compute_util() const {
    return scalesim_total_ops > 0 ? scalesim_weighted_compute_util_sum / scalesim_total_ops : 0.0;
}

double Roofline::get_scalesim_weighted_avg_overall_util() const {
    return scalesim_total_ops > 0 ? scalesim_weighted_overall_util_sum / scalesim_total_ops : 0.0;
}