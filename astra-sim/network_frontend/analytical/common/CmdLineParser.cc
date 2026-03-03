/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "common/CmdLineParser.hh"

using namespace AstraSimAnalytical;

CmdLineParser::CmdLineParser(const char* const argv0) noexcept
    : options(argv0, "ASTRA-sim") {
    parsed = {};

    // define options
    define_options();
}

void CmdLineParser::define_options() noexcept {
    options.set_width(70).allow_unrecognised_options().add_options()(
        "workload-configuration", "Workload configuration file",
        cxxopts::value<std::string>())(
        "comm-group-configuration", "Communicator group configuration file",
        cxxopts::value<std::string>()->default_value("empty"))(
        "system-configuration", "System configuration file",
        cxxopts::value<std::string>())("remote-memory-configuration",
                                       "Remote memory configuration file",
                                       cxxopts::value<std::string>())(
        "network-configuration", "Network configuration file",
                                        cxxopts::value<std::string>())(
        "logging-configuration", "Logging configuration file",
        cxxopts::value<std::string>()->default_value("empty"))(
        "num-queues-per-dim", "Number of queues per each dimension",
        cxxopts::value<int>()->default_value("1"))(
        "compute-scale", "Compute scale",
        cxxopts::value<double>()->default_value("1"))(
        "comm-scale", "Communication scale",
        cxxopts::value<double>()->default_value("1"))(
        "injection-scale", "Injection scale",
        cxxopts::value<double>()->default_value("1"))(
        "rendezvous-protocol", "Whether to enable rendezvous protocol",
        cxxopts::value<bool>()->default_value("false"))(
        "traffic-stats-output", "Output path for P2P traffic statistics file",
        cxxopts::value<std::string>()->default_value("traffic_flow_stats.json"))(
        "enable-traffic-stats", "Enable/disable P2P traffic statistics output",
        cxxopts::value<bool>()->default_value("true"))(
        "traffic-stats-dir", "Directory for traffic statistics output",
        cxxopts::value<std::string>()->default_value("./my_results"))(
        "traffic-stats-filename", "Base filename for traffic statistics (without extension)",
        cxxopts::value<std::string>()->default_value("traffic_flow_stats"))(
        "enable-json-summary", "Enable JSON summary output instead of log output",
        cxxopts::value<bool>()->default_value("false"))(
        "json-summary-file", "JSON summary output file path",
        cxxopts::value<std::string>()->default_value("simulation_results.json"));
}

void CmdLineParser::parse(int argc, char* argv[]) noexcept {
    try {
        // try parsing command line options
        parsed = options.parse(argc, argv);
    } catch (const cxxopts::OptionException& e) {
        // error occurred
        std::cerr << "[Error] (AstraSim/analytical/common) "
                  << "Error parsing options: " << e.what() << std::endl;
        exit(-1);
    }
}

cxxopts::Options& CmdLineParser::get_options() {
    return options;
}
