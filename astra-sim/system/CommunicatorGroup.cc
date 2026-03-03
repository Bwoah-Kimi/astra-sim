/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/
#include "astra-sim/system/Sys.hh"

#include "astra-sim/system/CommunicatorGroup.hh"

#include <algorithm>

#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/CollectivePlan.hh"

using namespace AstraSim;

CommunicatorGroup::CommunicatorGroup(
    int id,
    std::vector<int> involved_NPUs,
    Sys* generator,
    AstraNetworkAPI* comm_NI) {
  set_id(id);
  this->involved_NPUs = involved_NPUs;
  this->generator = generator;
  this->comm_NI = comm_NI;
  std::sort(involved_NPUs.begin(), involved_NPUs.end());
}

CommunicatorGroup::~CommunicatorGroup() {
    for (auto cg : comm_plans) {
        CollectivePlan* cp = cg.second;
        delete cp;
    }
}

void CommunicatorGroup::set_id(int id) {
    assert(id > 0);
    this->id = id;
    this->num_streams = id * 1000000;
}

CollectivePlan* CommunicatorGroup::get_collective_plan(ComType comm_type) {
    if (comm_plans.find(comm_type) != comm_plans.end()) {
        return comm_plans[comm_type];
    }

    if (static_cast<uint64_t>(generator->total_nodes) == involved_NPUs.size()) {
        LogicalTopology* logical_topology =
            generator->get_logical_topology(comm_type);
        std::vector<CollectiveImpl*> collective_implementation =
            generator->get_collective_implementation(comm_type);
        std::vector<bool> dimensions_involved(10, true);
        bool should_be_removed = false;
        comm_plans[comm_type] =
            new CollectivePlan(logical_topology, collective_implementation,
                               dimensions_involved, should_be_removed);
        return comm_plans[comm_type];
    } else {
        bool all_same = true;
        double bandwidth = -1.0;
        if (involved_NPUs.size() > 1) {
            int src = involved_NPUs[0];
            int dest = involved_NPUs[1];
            bandwidth = comm_NI->get_link_bandwidth(src, dest);
            for (size_t i = 1; i < involved_NPUs.size() - 1; i++) {
                src = involved_NPUs[i];
                dest = involved_NPUs[i + 1];
                if (comm_NI->get_link_bandwidth(src, dest) != bandwidth) {
                    all_same = false;
                    break;
                }
            }
            if (all_same) {
                int last_src = involved_NPUs.back();
                int first_dest = involved_NPUs.front();
                if (comm_NI->get_link_bandwidth(last_src, first_dest) != bandwidth) {
                    all_same = false;
                }
            }
        }
        LogicalTopology* logical_topology = new RingTopology(
            RingTopology::Dimension::Local,
            generator->id,
            involved_NPUs,
            all_same,
            bandwidth);
        std::vector<CollectiveImpl*> collective_implementation{
            new CollectiveImpl(CollectiveImplType::Ring)};
        std::vector<bool> dimensions_involved(1, true);
        bool should_be_removed = true;
        comm_plans[comm_type] =
            new CollectivePlan(logical_topology, collective_implementation,
                               dimensions_involved, should_be_removed);
        return comm_plans[comm_type];
    }
    assert(false);
    return nullptr;
}
