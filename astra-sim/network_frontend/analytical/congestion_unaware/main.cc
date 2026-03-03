/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/common/Logging.hh"
#include "common/CmdLineParser.hh"
#include "congestion_unaware/CongestionUnawareNetworkApi.hh"
#include <astra-network-analytical/common/EventQueue.h>
#include <astra-network-analytical/common/NetworkParser.h>
#include <astra-network-analytical/congestion_unaware/Helper.h>
#include <remote_memory_backend/analytical/AnalyticalRemoteMemory.hh>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <json/json.hpp>

using namespace AstraSim;
using namespace Analytical;
using namespace AstraSimAnalytical;
using namespace AstraSimAnalyticalCongestionUnaware;
using namespace NetworkAnalytical;
using namespace NetworkAnalyticalCongestionUnaware;

int main(int argc, char* argv[]) {
    // Parse command line arguments
    auto cmd_line_parser = CmdLineParser(argv[0]);
    cmd_line_parser.parse(argc, argv);

    // Get command line arguments
    const auto workload_configuration =
        cmd_line_parser.get<std::string>("workload-configuration");
    const auto comm_group_configuration =
        cmd_line_parser.get<std::string>("comm-group-configuration");
    const auto system_configuration =
        cmd_line_parser.get<std::string>("system-configuration");
    const auto remote_memory_configuration =
        cmd_line_parser.get<std::string>("remote-memory-configuration");
    const auto network_configuration =
        cmd_line_parser.get<std::string>("network-configuration");
    const auto logging_configuration =
        cmd_line_parser.get<std::string>("logging-configuration");
    const auto num_queues_per_dim =
        cmd_line_parser.get<int>("num-queues-per-dim");
    const auto comm_scale = cmd_line_parser.get<double>("comm-scale");
    const auto injection_scale = cmd_line_parser.get<double>("injection-scale");
    const auto rendezvous_protocol =
        cmd_line_parser.get<bool>("rendezvous-protocol");

    // Get traffic statistics configuration from command line
    const bool enable_traffic_stats_output = cmd_line_parser.get<bool>("enable-traffic-stats");
    const std::string traffic_stats_dir = cmd_line_parser.get<std::string>("traffic-stats-dir");
    const std::string traffic_stats_filename = cmd_line_parser.get<std::string>("traffic-stats-filename");
    
    // Get JSON summary configuration from command line
    const bool enable_json_summary = cmd_line_parser.get<bool>("enable-json-summary");
    const std::string json_summary_file = cmd_line_parser.get<std::string>("json-summary-file");

    AstraSim::LoggerFactory::init(logging_configuration);

    // Instantiate event queue
    const auto event_queue = std::make_shared<EventQueue>();

    // Generate topology
    const auto network_parser = NetworkParser(network_configuration);
    const auto topology = construct_topology(network_parser);

    // Get topology information
    const auto npus_count = topology->get_npus_count();
    const auto npus_count_per_dim = topology->get_npus_count_per_dim();
    const auto dims_count = topology->get_dims_count();

    // Set up Network API
    CongestionUnawareNetworkApi::set_event_queue(event_queue);
    CongestionUnawareNetworkApi::set_topology(topology);

    // Initialize P2P traffic statistics (clear any previous data)
    CongestionUnawareNetworkApi::clear_traffic_stats();

    // Check if single device mode is enabled by reading system configuration
    bool single_device_mode = false;
    {
        std::ifstream config_file(system_configuration);
        if (config_file.is_open()) {
            nlohmann::json config_json;
            config_file >> config_json;
            if (config_json.contains("single-device-simulation")) {
                single_device_mode = (config_json["single-device-simulation"] != 0);
            }
        }
    }

    // Create ASTRA-sim related resources
    auto network_apis =
        std::vector<std::unique_ptr<CongestionUnawareNetworkApi>>();
    const auto memory_api =
        std::make_unique<AnalyticalRemoteMemory>(remote_memory_configuration);
    auto systems = std::vector<Sys*>();

    auto queues_per_dim = std::vector<int>();
    for (auto i = 0; i < dims_count; i++) {
        queues_per_dim.push_back(num_queues_per_dim);
    }

    if (single_device_mode) {
        std::cout << "[INFO] Single device simulation mode enabled - only simulating device 0" << std::endl;
        
        // Only create device 0 for single device mode
        auto network_api = std::make_unique<CongestionUnawareNetworkApi>(0);
        auto* const system =
            new Sys(0, workload_configuration, comm_group_configuration,
                    system_configuration, memory_api.get(), network_api.get(),
                    npus_count_per_dim, queues_per_dim, injection_scale,
                    comm_scale, rendezvous_protocol);

        network_apis.push_back(std::move(network_api));
        systems.push_back(system);
        
        // Only initiate simulation for device 0
        systems[0]->workload->fire();
    } else {
        std::cout << "[INFO] Multi-device simulation mode - simulating all " << npus_count << " devices" << std::endl;
        
        // Original multi-device logic
        for (int i = 0; i < npus_count; i++) {
            // create network and system
            auto network_api = std::make_unique<CongestionUnawareNetworkApi>(i);
            auto* const system =
                new Sys(i, workload_configuration, comm_group_configuration,
                        system_configuration, memory_api.get(), network_api.get(),
                        npus_count_per_dim, queues_per_dim, injection_scale,
                        comm_scale, rendezvous_protocol);

            // push back network and system
            network_apis.push_back(std::move(network_api));
            systems.push_back(system);
        }

        // Initiate simulation for all devices
        for (int i = 0; i < npus_count; i++) {
            systems[i]->workload->fire();
        }
    }

    // run simulation
    while (!event_queue->finished()) {
        event_queue->proceed();
    }

    // Output P2P traffic statistics before terminating simulation
    if (enable_traffic_stats_output) {
        // Generate timestamp for unique filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream filename_stream;
        filename_stream << traffic_stats_dir << "/" << traffic_stats_filename;
        
        std::string traffic_stats_file = filename_stream.str();
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(traffic_stats_dir);
        
        // Output traffic statistics
        CongestionUnawareNetworkApi::output_traffic_stats_to_file(traffic_stats_file);
        
        std::cout << "Traffic statistics saved to: " << traffic_stats_file << std::endl;
    } else {
        std::cout << "Traffic statistics output disabled." << std::endl;
    }

    // Output JSON summary if enabled
    if (enable_json_summary && !systems.empty()) {
        // Get summary data from the first system (sys[0]) since all systems have the same results
        auto* first_system = systems[0];
        nlohmann::json summary = first_system->workload->get_summary_json();
        
        // Add simulation mode information to summary
        summary["simulation_mode"] = single_device_mode ? "single_device" : "multi_device";
        summary["devices_simulated"] = single_device_mode ? 1 : npus_count;
        
        // Write JSON to file
        std::ofstream json_file(json_summary_file);
        if (json_file.is_open()) {
            json_file << summary.dump(4) << std::endl;
            json_file.close();
            std::cout << "JSON summary saved to: " << json_summary_file << std::endl;
        } else {
            std::cerr << "Error: Could not open JSON summary file: " << json_summary_file << std::endl;
        }
    }

    // terminate simulation
    for (auto* system : systems) {
        delete system;
    }
    AstraSim::LoggerFactory::shutdown();
    return 0;
}
