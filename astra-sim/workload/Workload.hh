/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#ifndef __WORKLOAD_HH__
#define __WORKLOAD_HH__

#include <memory>
#include <string>
#include <unordered_map>
#include <json/json.hpp>

#include "astra-sim/system/Callable.hh"
#include "astra-sim/system/CommunicatorGroup.hh"
#include "astra-sim/workload/HardwareResource.hh"
#include "extern/graph_frontend/chakra/src/feeder/et_feeder.h"

namespace AstraSim {

  class Sys;
  class DataSet;

  class Workload : public Callable {
  public:
    Workload(Sys* sys,
      std::string et_filename,
      std::string comm_group_filename);
    ~Workload();

    // communicator groups
    // Parse the user provided 'comm_group_filename' and extract the list of communicator groups.
    // Refer to the wiki for the format.
    void initialize_comm_groups(std::string comm_group_filename);

    // event-based simulation
    void issue_dep_free_nodes();
    void issue(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_replay(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_remote_mem(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_comp(std::shared_ptr<Chakra::ETFeederNode> node);
    void issue_comm(std::shared_ptr<Chakra::ETFeederNode> node);
    void skip_invalid(std::shared_ptr<Chakra::ETFeederNode> node);
    void call(EventType event, CallData* data);
    void fire();

    // stats
    void report();
    nlohmann::json get_summary_json();

    Chakra::ETFeeder* et_feeder;
    std::unordered_map<int, CommunicatorGroup*> comm_groups;
    HardwareResource* hw_resource;
    Sys* sys;
    std::unordered_map<int, uint64_t> collective_comm_node_id_map;
    std::unordered_map<int, DataSet*> collective_comm_wrapper_map;
    bool is_finished;
    std::map<uint64_t, uint64_t> in_flight_ops_start_time;

  private:
    // From the ET node, find out the corresponding communicator group, and return the pointer.
    // If no communicator group is specified for this ET node, return nullptr.
    CommunicatorGroup* extract_comm_group(std::shared_ptr<Chakra::ETFeederNode> node);

    // ==== BEGIN: SCALE-SIM INTEGRATION ====
    // Block-level optimizations
    bool block_level_optimization_enabled = false;
    int current_block_id = -1;
    Tick first_block_latency = 0;
    Tick block_tracking_start_time = 0;
    std::unordered_set<uint64_t> first_block_ongoing_nodes;
    std::unordered_map<int, std::vector<uint64_t>> block_to_nodes_map;
    std::unordered_set<int> fast_forwarded_blocks;

    // Helper function to parse block_id from node name
    int extract_block_id_from_name(const std::string& name);

    // ==== END: SCALE-SIM INTEGRATION ====
  };

}  // namespace AstraSim

#endif /* __WORKLOAD_HH__ */
