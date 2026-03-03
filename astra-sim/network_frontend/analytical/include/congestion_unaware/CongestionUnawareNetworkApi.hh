/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#pragma once

#include "common/CommonNetworkApi.hh"
#include <astra-network-analytical/common/Type.h>
#include <astra-network-analytical/congestion_unaware/Topology.h>
#include <vector>
#include <map>
#include <string>
#include <mutex>

using namespace AstraSim;
using namespace AstraSimAnalytical;
using namespace NetworkAnalytical;
using namespace NetworkAnalyticalCongestionUnaware;

namespace AstraSimAnalyticalCongestionUnaware {

/**
 * CongestionUnawareNetworkApi is a AstraNetworkAPI
 * implemented for congestion_unaware analytical network backend.
 */
class CongestionUnawareNetworkApi final : public CommonNetworkApi {
  public:
    /**
     * Set the topology to be used.
     *
     * @param topology_ptr pointer to the to
     */
    static void set_topology(std::shared_ptr<Topology> topology_ptr) noexcept;

    /**
     * Constructor.
     *
     * @param rank id of the API
     */
    explicit CongestionUnawareNetworkApi(int rank) noexcept;

    /**
     * Implement sim_send of AstraNetworkAPI.
     */
    int sim_send(void* buffer,
                 uint64_t count,
                 int type,
                 int dst,
                 int tag,
                 sim_request* request,
                 void (*msg_handler)(void* fun_arg),
                 void* fun_arg) override;

    /**
     * @brief Get the topology object
     * 
     * @return std::shared_ptr<Topology> 
     */
    std::shared_ptr<Topology> get_topology() const noexcept;

    /**
     * Implement get_link_bandwidth of AstraNetworkAPI.
     */
    double get_link_bandwidth(int src, int dest) override;

    /**
     * @brief Get P2P traffic statistics
     * 
     * @return std::map<std::pair<int, int>, uint64_t> Map of (src, dst) -> total_bytes
     */
    static std::map<std::pair<int, int>, uint64_t> get_p2p_traffic_stats();

    /**
     * @brief Output traffic statistics to file
     * 
     * @param filename Output filename
     */
    static void output_traffic_stats_to_file(const std::string& filename);

    /**
     * @brief Clear traffic statistics
     */
    static void clear_traffic_stats();

  private:
    /// topology
    static std::shared_ptr<Topology> topology;

    /**
     * @brief Record P2P traffic statistics
     * 
     * @param src Source node ID
     * @param dst Destination node ID
     * @param count Number of bytes transferred
     * @param tag Communication tag
     */
    void record_p2p_traffic(int src, int dst, uint64_t count, int tag);

    /// Static P2P traffic statistics: (src, dst) -> total_bytes
    static std::map<std::pair<int, int>, uint64_t> p2p_traffic_stats;
    
    /// Static mutex for thread-safe access to traffic stats
    static std::mutex traffic_stats_mutex;
};

}  // namespace AstraSimAnalyticalCongestionUnaware
