/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/system/astraccl/native_collectives/collective_algorithm/Ring.hh"

#include "astra-sim/system/PacketBundle.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"

#include "astra-network-analytical/congestion_unaware/MultiDimTopology.h"
#include <stdlib.h>
using namespace AstraSim;

Ring::Ring(ComType type,
    int id,
    RingTopology* ring_topology,
    uint64_t data_size,
    RingTopology::Direction direction,
    InjectionPolicy injection_policy)
    : Algorithm() {
    this->comType = type;
    this->id = id;
    this->logical_topo = ring_topology;
    this->data_size = data_size;
    this->direction = direction;
    this->nodes_in_ring = ring_topology->get_nodes_in_ring();
    this->curr_receiver = ring_topology->get_receiver(id, direction);
    this->curr_sender = ring_topology->get_sender(id, direction);
    this->parallel_reduce = 1;
    this->injection_policy = injection_policy;
    this->total_packets_sent = 0;
    this->total_packets_received = 0;
    this->free_packets = 0;
    this->zero_latency_packets = 0;
    this->non_zero_latency_packets = 0;
    this->toggle = false;
    this->name = Name::Ring;
    if (ring_topology->get_dimension() == RingTopology::Dimension::Local) {
        transmition = MemBus::Transmition::Fast;
    }
    else {
        transmition = MemBus::Transmition::Usual;
    }
    switch (type) {
    case ComType::All_Reduce:
        stream_count = 2 * (nodes_in_ring - 1);
        break;
    case ComType::All_to_All:
        this->stream_count = ((nodes_in_ring - 1) * nodes_in_ring) / 2;
        switch (injection_policy) {
        case InjectionPolicy::Aggressive:
            this->parallel_reduce = nodes_in_ring - 1;
            break;
        case InjectionPolicy::Normal:
            this->parallel_reduce = 1;
            break;
        default:
            this->parallel_reduce = 1;
            break;
        }
        break;
    default:
        stream_count = nodes_in_ring - 1;
    }
    if (type == ComType::All_to_All || type == ComType::All_Gather) {
        max_count = 0;
    }
    else {
        max_count = nodes_in_ring - 1;
    }
    remained_packets_per_message = 1;
    remained_packets_per_max_count = 1;
    switch (type) {
    case ComType::All_Reduce:
        this->final_data_size = data_size;
        this->msg_size = data_size / nodes_in_ring;
        break;
    case ComType::All_Gather:
        this->final_data_size = data_size * nodes_in_ring;
        this->msg_size = data_size;
        break;
    case ComType::Reduce_Scatter:
        this->final_data_size = data_size / nodes_in_ring;
        this->msg_size = data_size / nodes_in_ring;
        break;
    case ComType::All_to_All:
        this->final_data_size = data_size;
        this->msg_size = data_size / nodes_in_ring;
        break;
    default:;
    }
}

int Ring::get_non_zero_latency_packets() {
    return (nodes_in_ring - 1) * parallel_reduce * 1;
}

void Ring::run(EventType event, CallData* data) {
    // std::cout << "[RING_RUN] node_id=" << id 
    //           << ", tick=" << Sys::boostedTick() 
    //           << ", event=" << static_cast<int>(event)
    //           << ", stream_count=" << stream_count
    //           << ", free_packets=" << free_packets << std::endl;

    if (event == EventType::General) {
        free_packets += 1;
        // std::cout << "[PACKET_RELEASE] node_id=" << id 
        //           << ", tick=" << Sys::boostedTick() 
        //           << ", free_packets=" << free_packets 
        //           << ", stream_count=" << stream_count << std::endl;
        ready();
        iteratable();
    }
    else if (event == EventType::PacketReceived) {
        // std::cout << "[PACKET_RECEIVED] node_id=" << id 
        //           << ", tick=" << Sys::boostedTick() 
        //           << ", total_packets_received=" << total_packets_received << std::endl;
        total_packets_received++;
        insert_packet(nullptr);
    }
    else if (event == EventType::StreamInit) {
        // std::cout << "[STREAM_INIT] node_id=" << id 
        //           << ", tick=" << Sys::boostedTick() 
        //           << ", parallel_reduce=" << parallel_reduce << std::endl;
        for (int i = 0; i < parallel_reduce; i++) {
            insert_packet(nullptr);
        }
    }
}

void Ring::release_packets() {
    for (auto packet : locked_packets) {
        packet->set_notifier(this);
    }
    if (NPU_to_MA == true) {
        (new PacketBundle(stream->owner, stream, locked_packets, processed,
            send_back, msg_size, transmition))
            ->send_to_MA();
    }
    else {
        (new PacketBundle(stream->owner, stream, locked_packets, processed,
            send_back, msg_size, transmition))
            ->send_to_NPU();
    }
    locked_packets.clear();
}

void Ring::process_stream_count() {
    if (remained_packets_per_message > 0) {
        remained_packets_per_message--;
    }
    if (id == 0) {
    }
    if (remained_packets_per_message == 0 && stream_count > 0) {
        stream_count--;
        if (stream_count > 0) {
            remained_packets_per_message = 1;
        }
    }
    if (remained_packets_per_message == 0 && stream_count == 0 &&
        stream->state != StreamState::Dead) {
        stream->changeState(StreamState::Zombie);
    }
}

void Ring::process_max_count() {
    if (remained_packets_per_max_count > 0) {
        remained_packets_per_max_count--;
    }
    if (remained_packets_per_max_count == 0) {
        max_count--;
        release_packets();
        remained_packets_per_max_count = 1;
    }
}

void Ring::reduce() {
    process_stream_count();
    packets.pop_front();
    free_packets--;
    total_packets_sent++;
}

bool Ring::iteratable() {
    // std::cout << "[ITERATABLE_CHECK] node_id=" << id 
    //           << ", tick=" << Sys::boostedTick() 
    //           << ", stream_count=" << stream_count
    //           << ", free_packets=" << free_packets
    //           << ", parallel_reduce=" << parallel_reduce << std::endl;

    if (stream_count == 0 &&
        free_packets == (parallel_reduce * 1)) {  // && not_delivered==0
        // std::cout << "[RING_EXIT] node_id=" << id 
        //           << ", tick=" << Sys::boostedTick() 
        //           << ", stream_count=" << stream_count 
        //           << ", free_packets=" << free_packets << std::endl;
        exit();
        return false;
    }
    return true;
}

void Ring::insert_packet(Callable* sender) {
    if (zero_latency_packets == 0 && non_zero_latency_packets == 0) {
        zero_latency_packets = parallel_reduce * 1;
        non_zero_latency_packets =
            get_non_zero_latency_packets();  //(nodes_in_ring-1)*parallel_reduce*1;
        toggle = !toggle;
    }
    if (zero_latency_packets > 0) {
        packets.push_back(MyPacket(
            stream->current_queue_id, curr_sender,
            curr_receiver));  // vnet Must be changed for alltoall topology
        packets.back().sender = sender;
        locked_packets.push_back(&packets.back());
        processed = false;
        send_back = false;
        NPU_to_MA = true;
        process_max_count();
        zero_latency_packets--;
        return;
    }
    else if (non_zero_latency_packets > 0) {
        packets.push_back(MyPacket(
            stream->current_queue_id, curr_sender,
            curr_receiver));  // vnet Must be changed for alltoall topology
        packets.back().sender = sender;
        locked_packets.push_back(&packets.back());
        if (comType == ComType::Reduce_Scatter ||
            (comType == ComType::All_Reduce && toggle)) {
            processed = true;
        }
        else {
            processed = false;
        }
        if (non_zero_latency_packets <= parallel_reduce * 1) {
            send_back = false;
        }
        else {
            send_back = true;
        }
        NPU_to_MA = false;
        process_max_count();
        non_zero_latency_packets--;
        return;
    }
    Sys::sys_panic("should not inject nothing!");
}

bool Ring::ready() {
    // std::cout << "[READY_CHECK] node_id=" << id 
    //           << ", tick=" << Sys::boostedTick() 
    //           << ", packets.size=" << packets.size()
    //           << ", stream_count=" << stream_count
    //           << ", free_packets=" << free_packets << std::endl;

    if (stream->state == StreamState::Created ||
        stream->state == StreamState::Ready) {
        stream->changeState(StreamState::Executing);
    }
    if (packets.size() == 0 || stream_count == 0 || free_packets == 0) {
        // std::cout << "[READY_BLOCKED] node_id=" << id 
        //           << ", packets.size=" << packets.size()
        //           << ", stream_count=" << stream_count
        //           << ", free_packets=" << free_packets << std::endl;
        return false;
    }
    MyPacket packet = packets.front();
    sim_request snd_req;
    snd_req.srcRank = id;
    snd_req.dstRank = packet.preferred_dest;
    snd_req.tag = stream->stream_id;
    snd_req.reqType = UINT8;
    snd_req.vnet = this->stream->current_queue_id;
    // Print timing info for network send call
    // std::cout << "[TIMING] NetworkSend: sys_id=" << id 
    //           << ", tick=" << Sys::boostedTick() 
    //           << ", stream_id=" << stream->stream_id
    //           << ", src=" << id << ", dst=" << packet.preferred_dest 
    //           << ", msg_size=" << msg_size 
    //           << ", queue_id=" << stream->current_queue_id << std::endl;

    // std::cout << "[SEND_CALL] node_id=" << id 
    //           << ", tick=" << Sys::boostedTick() 
    //           << ", calling front_end_sim_send" << std::endl;

    int actual_send_delay = stream->owner->front_end_sim_send(
        0, Sys::dummy_data, msg_size, UINT8, packet.preferred_dest,
        stream->stream_id, &snd_req, Sys::FrontEndSendRecvType::COLLECTIVE,
        &Sys::handleEvent,
        nullptr);  // stream_id+(packet.preferred_dest*50)

    // std::cout << "[SEND_CALL_DONE] node_id=" << id
    //     << ", tick=" << Sys::boostedTick()
    //     << ", actual_delay_returned=" << actual_send_delay << std::endl;

    // Handle recv based on single device mode
    if (stream->owner->single_device_mode) {
        // In single device mode, schedule recv completion to occur at the same time as send completion
        // This avoids immediate processing while maintaining correct timing

        RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
            stream, stream->owner->id, EventType::PacketReceived,
            packet.preferred_vnet, packet.stream_id);

        // Use the actual delay returned from front_end_sim_send
        // This ensures recv delay is exactly the same as send delay
        uint64_t send_delay = static_cast<uint64_t>(actual_send_delay);

        // std::cout << "[RECV_DELAY_FROM_SEND] node_id=" << id
        //     << ", using_actual_send_delay=" << send_delay << std::endl;
        // std::cout << "[RECV_SCHEDULED] node_id=" << id
        //     << ", tick=" << Sys::boostedTick()
        //     << ", delay=" << send_delay
        //     << ", complete_at=" << (Sys::boostedTick() + send_delay) << std::endl;


        // Use the same scheduling mechanism as send events
        timespec_t recv_delta;
        recv_delta.time_res = NS;
        recv_delta.time_val = static_cast<double>(send_delay);

        // Schedule recv completion to network EventQueue
        stream->owner->comm_NI->sim_schedule(
            recv_delta,
            [](void* arg) {
                // This callback will be executed when recv completes
                auto* recv_ehd = static_cast<RecvPacketEventHandlerData*>(arg);
                if (recv_ehd && recv_ehd->owner) {
                    // std::cout << "[RECV_CALLBACK] node_id=" << recv_ehd->owner->owner->id
                    //     << ", tick=" << Sys::boostedTick()
                    //     << ", recv event triggered from network EventQueue" << std::endl;

                    // Trigger the Ring's PacketReceived event
                    recv_ehd->owner->my_current_phase.algorithm->run(EventType::PacketReceived, nullptr);
                }
                delete recv_ehd;
            },
            ehd
        );

        // std::cout << "[RECV_REGISTER_DONE] node_id=" << id
        //     << ", tick=" << Sys::boostedTick()
        //     << ", recv event scheduled to network EventQueue" << std::endl;

    }
    else {
        // Normal multi-device mode - issue actual recv
        sim_request rcv_req;
        rcv_req.vnet = this->stream->current_queue_id;
        RecvPacketEventHandlerData* ehd = new RecvPacketEventHandlerData(
            stream, stream->owner->id, EventType::PacketReceived,
            packet.preferred_vnet, packet.stream_id);
        stream->owner->front_end_sim_recv(
            0, Sys::dummy_data, msg_size, UINT8, packet.preferred_src,
            stream->stream_id, &rcv_req, Sys::FrontEndSendRecvType::COLLECTIVE,
            &Sys::handleEvent,
            ehd);  // stream_id+(owner->id*50)
    }
    reduce();
    return true;
}

void Ring::exit() {
    if (packets.size() != 0) {
        packets.clear();
    }
    if (locked_packets.size() != 0) {
        locked_packets.clear();
    }
    stream->owner->proceed_to_next_vnet_baseline((StreamBaseline*)stream);
    return;
}
