/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/workload/HardwareResource.hh"
#include "astra-sim/system/Sys.hh"
#include <cassert>

using namespace std;
using namespace AstraSim;
using namespace Chakra;

typedef ChakraProtoMsg::NodeType ChakraNodeType;

HardwareResource::HardwareResource(uint32_t num_npus)
    : num_npus(num_npus),
    num_in_flight_cpu_ops(0),
    num_in_flight_gpu_comm_ops(0),
    num_in_flight_gpu_comp_ops(0) {

    // Initialize all statistical counters
    num_cpu_ops = 0;
    num_gpu_ops = 0;
    num_gpu_comms = 0;
    tics_cpu_ops = 0;
    tics_gpu_ops = 0;
    tics_gpu_comms = 0;
    tics_gpu_p2p_comms = 0;
    tics_gpu_remote_mem = 0;
    tics_gpu_idle = 0;
    last_gpu_busy_finish_time = 0;
    total_in_flight_gpu_ops = 0;
}

bool HardwareResource::is_gpu_busy() const {
    // This is for statistical purpose only, checking the overall GPU state.
    return total_in_flight_gpu_ops > 0;
}

void HardwareResource::occupy(const shared_ptr<Chakra::ETFeederNode> node) {
    if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
        // RECV operation is special, it does not occupy any resource tracked here.
        // It is merely a request posting.
        return;
    }

    // Idle time statistics (does not affect simulation logic)
    if (total_in_flight_gpu_ops == 0 && !node->is_cpu_op()) {
        Tick current_time = Sys::boostedTick();
        tics_gpu_idle += (current_time - last_gpu_busy_finish_time);
    }

    // Increment total GPU ops counter for idle statistics before handling contention
    if (!node->is_cpu_op()) {
        total_in_flight_gpu_ops++;
    }

    // Original simulation logic for resource contention
    if (node->is_cpu_op()) {
        assert(num_in_flight_cpu_ops == 0);
        ++num_in_flight_cpu_ops;
    }
    else {
        if (node->type() == ChakraNodeType::COMP_NODE) {
            assert(num_in_flight_gpu_comp_ops == 0);
            ++num_in_flight_gpu_comp_ops;
        }
        else if (node->type() == ChakraNodeType::COMM_COLL_NODE ||
            node->type() == ChakraNodeType::COMM_SEND_NODE) {
            // RECV operations do not occupy the comm resource
            assert(num_in_flight_gpu_comm_ops == 0);
            ++num_in_flight_gpu_comm_ops;
        }
    }
}

void HardwareResource::release(const shared_ptr<Chakra::ETFeederNode> node) {
    if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
        // RECV operation did not occupy any resource, so it does nothing on release.
        return;
    }

    // Decrement total GPU ops counter and update finish time for idle statistics first
    if (!node->is_cpu_op()) {
        total_in_flight_gpu_ops--;
        if (total_in_flight_gpu_ops == 0) {
            last_gpu_busy_finish_time = Sys::boostedTick();
        }
    }

    // Original simulation logic for resource contention
    if (node->is_cpu_op()) {
        --num_in_flight_cpu_ops;
        assert(num_in_flight_cpu_ops == 0);
    }
    else {
        if (node->type() == ChakraNodeType::COMP_NODE) {
            --num_in_flight_gpu_comp_ops;
            assert(num_in_flight_gpu_comp_ops == 0);
        }
        else if (node->type() == ChakraNodeType::COMM_COLL_NODE ||
            node->type() == ChakraNodeType::COMM_SEND_NODE) {
            // RECV operations did not occupy the resource, so they don't release it
            --num_in_flight_gpu_comm_ops;
            assert(num_in_flight_gpu_comm_ops == 0);
        }
    }
}

bool HardwareResource::is_available(
    const shared_ptr<Chakra::ETFeederNode> node) const {
    if (node->is_cpu_op()) {
        return (num_in_flight_cpu_ops == 0);
    }
    else {
        if (node->type() == ChakraNodeType::COMP_NODE) {
            return (num_in_flight_gpu_comp_ops == 0);
        }
        else if (node->type() == ChakraNodeType::COMM_COLL_NODE ||
            node->type() == ChakraNodeType::COMM_SEND_NODE) {
            return (num_in_flight_gpu_comm_ops == 0);
        }
        else if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
            // A RECV can always be posted, as it doesn't consume the comm resource itself
            return true;
        }
        else {
            // For MEM_LOAD/STORE, we assume they don't consume the tracked hardware resources
            // This matches the original logic where they were not handled here.
            return true;
        }
    }
}

void HardwareResource::report() {
    // This function is now empty as the reporting is handled by Workload::report().
}
