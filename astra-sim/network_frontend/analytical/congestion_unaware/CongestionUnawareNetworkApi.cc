/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "congestion_unaware/CongestionUnawareNetworkApi.hh"
#include <cassert>
#include "astra-network-analytical/congestion_unaware/MultiDimTopology.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>

using namespace AstraSim;
using namespace AstraSimAnalyticalCongestionUnaware;
using namespace NetworkAnalytical;
using namespace NetworkAnalyticalCongestionUnaware;

std::shared_ptr<Topology> CongestionUnawareNetworkApi::topology;

// Initialize static member variables
std::map<std::pair<int, int>, uint64_t> CongestionUnawareNetworkApi::p2p_traffic_stats;
std::mutex CongestionUnawareNetworkApi::traffic_stats_mutex;

void CongestionUnawareNetworkApi::set_topology(
    std::shared_ptr<Topology> topology_ptr) noexcept {
    assert(topology_ptr != nullptr);

    // move topology
    CongestionUnawareNetworkApi::topology = std::move(topology_ptr);

    // set topology-related values
    CongestionUnawareNetworkApi::dims_count =
        CongestionUnawareNetworkApi::topology->get_dims_count();
    CongestionUnawareNetworkApi::bandwidth_per_dim =
        CongestionUnawareNetworkApi::topology->get_bandwidth_per_dim();
}

CongestionUnawareNetworkApi::CongestionUnawareNetworkApi(
    const int rank) noexcept
    : CommonNetworkApi(rank) {
    assert(rank >= 0);
}

int CongestionUnawareNetworkApi::sim_send(void* const buffer,
    const uint64_t count,
    const int type,
    const int dst,
    const int tag,
    sim_request* const request,
    void (*msg_handler)(void*),
    void* const fun_arg) {
    // query chunk id
    const auto src = sim_comm_get_rank();
    // DEBUG: Print P2P communication start
    // std::cout << "[P2P_SEND] src=" << src 
    //           << " -> dst=" << dst 
    //           << ", bytes=" << count 
    //           << ", tag=" << tag 
    //           << ", start_tick=" << event_queue->get_current_time() << std::endl;

    // Record P2P traffic statistics
    record_p2p_traffic(src, dst, count, tag);

    const auto chunk_id =
        CongestionUnawareNetworkApi::chunk_id_generator.create_send_chunk_id(
            tag, src, dst, count);

    // search tracker
    const auto entry =
        callback_tracker.search_entry(tag, src, dst, count, chunk_id);
    if (entry.has_value()) {
        // recv operation already issued.
        // add send event handler to the tracker
        entry.value()->register_send_callback(msg_handler, fun_arg);
    }
    else {
        // recv operation not issued yet
        // create new entry and insert send callback
        auto* const new_entry =
            callback_tracker.create_new_entry(tag, src, dst, count, chunk_id);
        new_entry->register_send_callback(msg_handler, fun_arg);
    }

    // create chunk
    auto chunk_arrival_arg = std::tuple(tag, src, dst, count, chunk_id);
    auto arg = std::make_unique<decltype(chunk_arrival_arg)>(chunk_arrival_arg);
    const auto arg_ptr = static_cast<void*>(arg.release());

    // compute send communication delay (in AstraSim format)
    const auto send_delay_ns = topology->send(src, dst, count);
    const auto send_delay = static_cast<double>(send_delay_ns);
    const auto delta = timespec_t({ NS, send_delay });

    // DEBUG: Print P2P communication completion time
    // std::cout << "[P2P_DELAY] src=" << src 
    //           << " -> dst=" << dst 
    //           << ", delay_ns=" << send_delay_ns 
    //           << ", complete_tick=" << (event_queue->get_current_time() + send_delay_ns) << std::endl;    
    // register chunk arrival event after send communication delay
    sim_schedule(delta, CongestionUnawareNetworkApi::process_chunk_arrival,
        arg_ptr);

    // return the actual delay value instead of 0
    return static_cast<int>(send_delay_ns);
}

std::shared_ptr<Topology> CongestionUnawareNetworkApi::get_topology()
const noexcept {
    return topology;
}

double CongestionUnawareNetworkApi::get_link_bandwidth(int src, int dest) {
    auto topology_unaware = std::dynamic_pointer_cast<
        NetworkAnalyticalCongestionUnaware::MultiDimTopology>(
            CongestionUnawareNetworkApi::topology);
    if (topology_unaware) {
        if (src == dest) {
            return -1.0;  // Cannot get bandwidth for the same node
        }
        const auto src_address = topology_unaware->translate_address(src);
        const auto dest_address = topology_unaware->translate_address(dest);

        const auto dim_to_transfer =
            topology_unaware->get_dim_to_transfer(src_address, dest_address);

        return topology_unaware->get_bandwidth_per_dim()[dim_to_transfer];
    }
    return -1.0;
}

void CongestionUnawareNetworkApi::record_p2p_traffic(int src, int dst, uint64_t count, int tag) {
    std::lock_guard<std::mutex> lock(traffic_stats_mutex);

    // Create a pair for the source-destination mapping
    auto node_pair = std::make_pair(src, dst);

    // Add to the traffic statistics
    p2p_traffic_stats[node_pair] += count;
}

std::map<std::pair<int, int>, uint64_t> CongestionUnawareNetworkApi::get_p2p_traffic_stats() {
    std::lock_guard<std::mutex> lock(traffic_stats_mutex);
    return p2p_traffic_stats;
}

void CongestionUnawareNetworkApi::output_traffic_stats_to_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(traffic_stats_mutex);

    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << " for writing" << std::endl;
        return;
    }

    // Calculate total traffic
    uint64_t total_bytes = 0;
    for (const auto& pair : p2p_traffic_stats) {
        total_bytes += pair.second;
    }

    // Output JSON format
    outfile << "{\n";
    outfile << "  \"simulation_info\": {\n";
    outfile << "    \"total_node_pairs\": " << p2p_traffic_stats.size() << ",\n";
    outfile << "    \"total_node\": " << (topology ? topology->get_npus_count() : 0) << ",\n";
    outfile << "    \"total_bytes\": " << total_bytes << ",\n";
    outfile << "    \"total_gb\": " << std::fixed << std::setprecision(2) << (total_bytes / (1024.0 * 1024.0 * 1024.0)) << "\n";
    outfile << "  },\n";
    outfile << "  \"p2p_traffic\": {\n";

    bool first = true;
    for (const auto& pair : p2p_traffic_stats) {
        if (!first) {
            outfile << ",\n";
        }
        first = false;

        int src = pair.first.first;
        int dst = pair.first.second;
        uint64_t bytes = pair.second;

        outfile << "    \"" << src << "_" << dst << "\": {\n";
        outfile << "      \"src\": " << src << ",\n";
        outfile << "      \"dst\": " << dst << ",\n";
        outfile << "      \"bytes\": " << bytes << ",\n";
        outfile << "      \"gb\": " << std::fixed << std::setprecision(6) << (bytes / (1024.0 * 1024.0 * 1024.0)) << "\n";
        outfile << "    }";
    }

    outfile << "\n  }\n";
    outfile << "}\n";

    outfile.close();

    // std::cout << "Traffic statistics written to " << filename << std::endl;
    // std::cout << "Total P2P traffic: " << total_bytes << " bytes ("
    //     << std::fixed << std::setprecision(2) << (total_bytes / (1024.0 * 1024.0 * 1024.0)) << " GB)" << std::endl;
}

void CongestionUnawareNetworkApi::clear_traffic_stats() {
    std::lock_guard<std::mutex> lock(traffic_stats_mutex);
    p2p_traffic_stats.clear();
}
