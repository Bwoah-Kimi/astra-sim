/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "astra-sim/workload/Workload.hh"

#include "astra-sim/common/Logging.hh"
#include "astra-sim/system/IntData.hh"
#include "astra-sim/system/MemEventHandlerData.hh"
#include "astra-sim/system/RecvPacketEventHandlerData.hh"
#include "astra-sim/system/SendPacketEventHandlerData.hh"
#include "astra-sim/system/WorkloadLayerHandlerData.hh"
#include <json/json.hpp>

#include <iostream>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <sys/types.h>
#include <array>
#include <memory>
#include <map>
#include <tuple>
#include <filesystem>
#include <set>
#include <algorithm>
#include <cstdlib>


using namespace std;
using namespace AstraSim;
using namespace Chakra;
using json = nlohmann::json;

typedef ChakraProtoMsg::NodeType ChakraNodeType;
typedef ChakraProtoMsg::CollectiveCommType ChakraCollectiveCommType;

uint64_t gent_env_var(const std::string& env_var_name, uint64_t default_val) {
    const char* val_str = std::getenv(env_var_name.c_str());
    if (val_str == nullptr) {
        return default_val;
    }

    try {
        return std::stoull(val_str);
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Could not parse environment variable " << env_var_name
            << ". Using default value: " << default_val << std::endl;
        return default_val;
    }
}

std::string get_required_env_var(const std::string& env_var_name) {
    const char* val_str = std::getenv(env_var_name.c_str());
    if (val_str == nullptr || std::string(val_str).empty()) {
        std::cerr << "AstraSim Error: Required environment variable " << env_var_name
            << " is not set." << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return std::string(val_str);
}

bool file_exists(const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

void create_scalesim_config(const std::string& config_path, uint64_t M, uint64_t N, uint64_t K, uint64_t array_height, uint64_t array_width, uint64_t bandwidth, uint64_t SRAM_size_kB, const std::string& dataflow, const std::string& run_name) {
    std::ofstream config_file(config_path);

    double bias_if = 1.0, bias_filt = 1.0, bias_of = 1.0;
    if (dataflow == "os")      bias_of = 1.6;   // output-stationary
    else if (dataflow == "ws") bias_filt = 1.6;   // weight-stationary
    else if (dataflow == "is") bias_if = 1.6;   // input-stationary

    double w_if = static_cast<double>(M) * static_cast<double>(K) * bias_if;
    double w_f = static_cast<double>(K) * static_cast<double>(N) * bias_filt;
    double w_of = static_cast<double>(M) * static_cast<double>(N) * bias_of;
    double w_sum = std::max(1.0, w_if + w_f + w_of);

    double if_share = SRAM_size_kB * (w_if / w_sum);
    double f_share = SRAM_size_kB * (w_f / w_sum);
    double of_share = SRAM_size_kB * (w_of / w_sum);

    uint64_t if_kB = std::max<uint64_t>(1, static_cast<uint64_t>(if_share));
    uint64_t f_kB = std::max<uint64_t>(1, static_cast<uint64_t>(f_share));
    uint64_t of_kB = std::max<uint64_t>(1, static_cast<uint64_t>(of_share));

    // Distribute remaining kB due to truncation to the largest fractional parts
    uint64_t assigned = if_kB + f_kB + of_kB;
    uint64_t rem = (SRAM_size_kB > assigned) ? (SRAM_size_kB - assigned) : 0;
    while (rem > 0) {
        double if_frac = if_share - static_cast<double>(if_kB);
        double f_frac = f_share - static_cast<double>(f_kB);
        double of_frac = of_share - static_cast<double>(of_kB);
        if (if_frac >= f_frac && if_frac >= of_frac) ++if_kB;
        else if (f_frac >= if_frac && f_frac >= of_frac) ++f_kB;
        else ++of_kB;
        --rem;
    }

    config_file << "[general]" << std::endl;
    config_file << "run_name = " << run_name << std::endl << std::endl;

    config_file << "[architecture_presets]" << std::endl;
    config_file << "ArrayHeight:    " << array_height << std::endl;
    config_file << "ArrayWidth:     " << array_width << std::endl;
    config_file << "IfmapSramSzkB:    " << if_kB << std::endl;
    config_file << "FilterSramSzkB:   " << f_kB << std::endl;
    config_file << "OfmapSramSzkB:    " << of_kB << std::endl;
    config_file << "IfmapOffset:    0" << std::endl;
    config_file << "FilterOffset:   10000000" << std::endl;
    config_file << "OfmapOffset:    20000000" << std::endl;
    config_file << "Dataflow: " << dataflow << std::endl;
    config_file << "Bandwidth: " << bandwidth << std::endl; // word/cycle
    config_file << "ReadRequestBuffer: 512" << std::endl;
    config_file << "WriteRequestBuffer: 512" << std::endl << std::endl;

    config_file << "[layout]" << std::endl;
    config_file << "IfmapCustomLayout: False" << std::endl;
    config_file << "IfmapSRAMBankBandwidth: " << bandwidth << std::endl; // DRAM bandwidth in word/cycle
    config_file << "IfmapSRAMBankNum: 1" << std::endl;
    config_file << "IfmapSRAMBankPort: 2" << std::endl;
    config_file << "FilterCustomLayout: False" << std::endl;
    config_file << "FilterSRAMBankBandwidth: " << bandwidth << std::endl; // DRAM bandwidth in word/cycle
    config_file << "FilterSRAMBankNum: 1" << std::endl;
    config_file << "FilterSRAMBankPort: 2" << std::endl << std::endl;

    config_file << "[sparsity]" << std::endl;
    config_file << "SparsitySupport: false" << std::endl << std::endl;
    config_file << "SparseRep: ellpack_block" << std::endl;
    config_file << "OptimizedMapping: false" << std::endl;
    config_file << "BlockSize: 8" << std::endl;
    config_file << "RandomNumberGeneratorSeed: 40" << std::endl << std::endl;

    config_file << "[run_presets]" << std::endl;
    config_file << "InterfaceBandwidth: USER" << std::endl;
    config_file << "UseRamulatorTrace: False" << std::endl;

    config_file.close();
}

void create_scalesim_topology(const std::string& topo_path, uint64_t M, uint64_t N, uint64_t K) {
    std::ofstream topo_file(topo_path);
    topo_file << "Layer,M,N,K," << std::endl;
    topo_file << "matmul," << M << "," << N << "," << K << "," << std::endl;
    topo_file.close();
}

void create_scalesim_output_path(const std::string& out_dir) {
    std::string cmd = "mkdir -p " + out_dir;
    system(cmd.c_str());
}

static inline std::string trim_ws(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, e - b + 1);
}

uint64_t parse_scalesim_result(
    const std::string& result_path,
    uint64_t* total_cycles_incl_prefetch /*=nullptr*/,
    double* overall_util /*=nullptr*/,
    double* mapping_efficiency /*=nullptr*/,
    double* compute_efficiency /*=nullptr*/
) {
    std::ifstream result_file(result_path);
    if (!result_file.is_open()) {
        std::cout << "AstraSim Error: Could not open SCALE-Sim result file: " << result_path << std::endl;
        return (uint64_t)-1; // keep previous behavior (non-zero on failure)
    }

    // Read header
    std::string header;
    if (!std::getline(result_file, header)) {
        std::cout << "AstraSim Error: Empty SCALE-Sim report file." << std::endl;
        return (uint64_t)1;
    }
    std::stringstream header_ss(header);
    std::vector<std::string> columns;
    std::string segment;
    while (std::getline(header_ss, segment, ',')) {
        auto col = trim_ws(segment);
        if (!col.empty()) columns.push_back(col);
    }

    // Locate columns
    int col_cycles = -1;
    int col_cycles_prefetch = -1;
    int col_overall_util = -1;
    int col_mapping_eff = -1;
    int col_compute_eff = -1;

    for (int i = 0; i < (int)columns.size(); ++i) {
        const auto& c = columns[i];
        if (c.find("Total Cycles (incl. prefetch)") != std::string::npos) col_cycles_prefetch = i;
        else if ((c == "Total Cycles") || (c.find("Total Cycles") != std::string::npos && c.find("prefetch") == std::string::npos)) col_cycles = i;
        else if (c.find("Overall Util") != std::string::npos) col_overall_util = i;
        else if (c.find("Mapping Efficiency") != std::string::npos) col_mapping_eff = i;
        else if (c.find("Compute Util") != std::string::npos || c.find("Compute Efficiency") != std::string::npos) col_compute_eff = i;
    }

    if (col_cycles == -1) {
        std::cout << "AstraSim Error: Could not find 'Total Cycles' in SCALE-Sim report." << std::endl;
        return (uint64_t)1;
    }

    // Read first data row
    std::string data_line;
    if (!std::getline(result_file, data_line)) {
        std::cout << "AstraSim Error: SCALE-Sim report has no data rows." << std::endl;
        return (uint64_t)1;
    }
    result_file.close();

    std::vector<std::string> values;
    std::stringstream data_ss(data_line);
    while (std::getline(data_ss, segment, ',')) {
        auto v = trim_ws(segment);
        if (!v.empty()) values.push_back(v);
    }

    auto to_uint = [&](int idx, uint64_t defv = 0ULL) -> uint64_t {
        if (idx < 0 || idx >= (int)values.size()) return defv;
        try { return (uint64_t)std::stoull(values[idx]); }
        catch (...) { return defv; }
        };
    auto to_double = [&](int idx, double defv = 0.0) -> double {
        if (idx < 0 || idx >= (int)values.size()) return defv;
        try { return std::stod(values[idx]); }
        catch (...) { return defv; }
        };

    uint64_t total_cycles = to_uint(col_cycles, 0ULL);

    if (total_cycles_incl_prefetch) *total_cycles_incl_prefetch = to_uint(col_cycles_prefetch, total_cycles);
    if (overall_util) *overall_util = to_double(col_overall_util, 0.0);
    if (mapping_efficiency) *mapping_efficiency = to_double(col_mapping_eff, 0.0);
    if (compute_efficiency) *compute_efficiency = to_double(col_compute_eff, 0.0);

    return total_cycles;
}

uint64_t run_scalesim_for_tile(
    uint64_t M, uint64_t N, uint64_t K, bool& success,
    uint64_t array_height, uint64_t array_width, uint64_t SRAM_size_kB, uint64_t DRAM_bw,
    uint64_t* compute_cycles, double* overall_util, double* mapping_eff, double* compute_util
) {
    std::string dataflow = "os";
    pid_t pid = getpid();

    std::string run_name = "scalesim_run_" + std::to_string(pid) + "_" + std::to_string(M) + "_" + std::to_string(N) + "_" + std::to_string(K);
    const std::string scalesim_input_dir = get_required_env_var("ASTRA_SCALESIM_INPUT_DIR");
    const std::string scalesim_output_dir = get_required_env_var("ASTRA_SCALESIM_OUTPUT_DIR");
    const std::string layout_path = get_required_env_var("ASTRA_SCALESIM_LAYOUT_PATH");
    const std::string scalesim_root = get_required_env_var("SCALESIM_ROOT");
    const std::string python_bin = get_required_env_var("ASTRA_PYTHON_BIN");

    const std::string config_path = (std::filesystem::path(scalesim_input_dir) / ("scalesim_config_" + run_name + ".cfg")).string();
    const std::string topo_path = (std::filesystem::path(scalesim_input_dir) / ("scalesim_topology_" + run_name + ".csv")).string();
    const std::filesystem::path out_dir_path = std::filesystem::path(scalesim_output_dir);
    const std::string out_dir = out_dir_path.string();

    create_scalesim_config(config_path, M, N, K, array_height, array_width, DRAM_bw, SRAM_size_kB, dataflow, run_name);
    create_scalesim_topology(topo_path, M, N, K);
    create_scalesim_output_path(out_dir);

    std::string python_cmd = python_bin + " " + scalesim_root + "/scalesim/scale.py -c " + config_path + " -t " + topo_path + " -l " + layout_path + " -i gemm -p " + out_dir;

    int ret = system(python_cmd.c_str());

    uint64_t total_cycles = 0;
    // uint64_t cycles = 0;
    // double overall_util = 0.0;
    // double mapping_eff = 0.0;
    // double compute_eff = 0.0;
    if (ret == 0) {
        std::string result_file_path = (out_dir_path / run_name / "COMPUTE_REPORT.csv").string();
        if (file_exists(result_file_path)) {
            uint64_t compute_cycles_val = parse_scalesim_result(result_file_path, &total_cycles, overall_util, mapping_eff, compute_util);
            if (compute_cycles) {
                *compute_cycles = compute_cycles_val;
            }
            if (compute_cycles_val > 0) {
                success = true;
            }
            else {
                std::cout << "AstraSim Error: Failed to parse cycles from SCALE-Sim result file." << std::endl;
                success = false;
            }
        }
        else {
            std::cout << "AstraSim Error: SCALE-Sim success, but result file is missing: " << result_file_path << std::endl;
            success = false;
        }
    }
    else {
        std::cout << "AstraSim Error: SCALE-Sim execution failed." << std::endl;
        success = false;
    }

    // Clean up temporary files
    remove(config_path.c_str());
    remove(topo_path.c_str());
    std::string rm_cmd = "rm -rf " + (out_dir_path / run_name).string();
    system(rm_cmd.c_str());

    return total_cycles;
}

uint64_t find_largest_factor_le(uint64_t n, uint64_t limit) {
    if (n == 0) return 0; // No factors for 0
    if (limit > n) limit = n;
    for (uint64_t i = limit; i != 0; --i) {
        if (n % i == 0) {
            return i;
        }
    }
    return 1; // Should not happen if n > 0
}

std::string get_current_working_directory() {
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
    // 使用 C++17 的 filesystem 库 (推荐)
    return std::filesystem::current_path().string();
#else
    // 使用 C-style 的 getcwd 作为备用
    char buff[FILENAME_MAX];
    GetCurrentDir(buff, FILENAME_MAX);
    std::string current_working_dir(buff);
    return current_working_dir;
#endif
}

uint64_t lookup_in_csv(
    const std::string& csv_file,
    uint64_t M, uint64_t N, uint64_t K,
    uint64_t sram_size, uint64_t bandwidth,
    uint64_t array_height, uint64_t array_width,
    double* mapping_eff, double* compute_util, double* overall_util
) {
    std::ifstream precomputed_file(csv_file);
    if (!precomputed_file.is_open()) {
        // File does not exist
        return 0;
    }

    std::string line;
    bool first_line = true;
    while (getline(precomputed_file, line)) {
        if (first_line) {
            first_line = false;
            if (line.find("M,N,K") != std::string::npos) {
                continue; // Skip header line
            }
        }

        std::stringstream ss(line);
        std::string m_str, n_str, k_str, sram_str, bw_str, height_str, width_str;
        std::string total_cycles_str, total_incl_prefetch_str, mapping_eff_str, compute_eff_str, overall_util_str;

        getline(ss, m_str, ',');
        getline(ss, n_str, ',');
        getline(ss, k_str, ',');
        getline(ss, height_str, ',');
        getline(ss, width_str, ',');
        getline(ss, sram_str, ',');
        getline(ss, bw_str, ',');
        getline(ss, total_incl_prefetch_str, ','); // runtime_cycles
        getline(ss, total_cycles_str, ','); // compute_cycles
        getline(ss, mapping_eff_str, ',');
        getline(ss, compute_eff_str, ',');
        getline(ss, overall_util_str, ',');

        // Sanity check
        if (m_str.empty() || n_str.empty() || k_str.empty() ||
            sram_str.empty() || bw_str.empty() || height_str.empty() || width_str.empty() ||
            total_cycles_str.empty() || total_incl_prefetch_str.empty() ||
            mapping_eff_str.empty() || compute_eff_str.empty() || overall_util_str.empty()) {
            continue; // Skip malformed lines
        }

        // Include bandwidth in matching conditions
        try {
            if (std::stoull(m_str) == M &&
                std::stoull(n_str) == N &&
                std::stoull(k_str) == K &&
                std::stoull(sram_str) == sram_size &&
                std::stoull(bw_str) == bandwidth &&
                std::stoull(height_str) == array_height &&
                std::stoull(width_str) == array_width) {

                uint64_t total_cycles_incl_prefetch = std::stoull(total_incl_prefetch_str);
                // Parse and return utilization metrics if pointers are provided
                if (mapping_eff != nullptr) {
                    *mapping_eff = std::stod(mapping_eff_str);
                }
                if (compute_util != nullptr) {
                    *compute_util = std::stod(compute_eff_str);
                }
                if (overall_util != nullptr) {
                    *overall_util = std::stod(overall_util_str);
                }

                precomputed_file.close();
                return total_cycles_incl_prefetch;
            }
        }
        catch (const std::invalid_argument& e) {
            continue;
        }
        catch (const std::out_of_range& e) {
            continue;
        }
    }
    precomputed_file.close();

    // No matching entry found
    return 0;
}

void append_to_lookup_csv(
    const std::string& csv_file, uint64_t M, uint64_t N, uint64_t K,
    uint64_t array_height, uint64_t array_width, uint64_t sram_size, uint64_t bandwidth,
    uint64_t runtime_cycles, uint64_t compute_cycles, double mapping_eff, double compute_util, double overall_util
) {
    bool file_exists = (access(csv_file.c_str(), F_OK) != -1);
    std::ofstream precomputed_file(csv_file, std::ios_base::app);

    // Add header if the file does not exist
    if (!file_exists) {
        precomputed_file << "M,N,K,array_height,array_width,sram_size,bandwidth,runtime_cycles,compute_cycles,mapping_eff,compute_util,overall_util\n";
    }

    precomputed_file << M << "," << N << "," << K << "," << array_height << "," << array_width << "," << sram_size << "," << bandwidth << "," << runtime_cycles << "," << compute_cycles << "," << mapping_eff << "," << compute_util << "," << overall_util << "\n";
    precomputed_file.close();
}

Workload::Workload(Sys* sys, string et_filename, string comm_group_filename) {
    string workload_filename = et_filename + "." + to_string(sys->id) + ".et";
    // Check if workload filename exists
    if (access(workload_filename.c_str(), R_OK) < 0) {
        string error_msg;
        if (errno == ENOENT) {
            error_msg =
                "workload file: " + workload_filename + " does not exist";
        }
        else if (errno == EACCES) {
            error_msg = "workload file: " + workload_filename +
                " exists but is not readable";
        }
        else {
            error_msg =
                "Unknown workload file: " + workload_filename + " access error";
        }
        LoggerFactory::get_logger("workload")->critical(error_msg);
        exit(EXIT_FAILURE);
    }
    this->et_feeder = new ETFeeder(workload_filename);
    this->comm_groups.clear();
    // TODO: parametrize the number of available hardware resources
    this->hw_resource = new HardwareResource(1);
    this->sys = sys;
    initialize_comm_groups(comm_group_filename);
    this->is_finished = false;

    // 重置 roofline 统计信息
    if (sys->roofline_enabled) {
        sys->roofline->reset_stats();
    }
}

Workload::~Workload() {
    for (auto comm_group : comm_groups) {
        delete comm_group.second;
    }
    comm_groups.clear();

    if (this->et_feeder != nullptr) {
        delete this->et_feeder;
    }
    if (this->hw_resource != nullptr) {
        delete this->hw_resource;
    }
}

void Workload::initialize_comm_groups(string comm_group_filename) {
    // communicator group input file is not given
    if (comm_group_filename.find("empty") != std::string::npos) {
        comm_groups.clear();
        return;
    }

    ifstream inFile;
    json j;
    inFile.open(comm_group_filename);
    inFile >> j;

    for (json::iterator it = j.begin(); it != j.end(); ++it) {
        std::string comm_group_name = it.key();
        int comm_group_id = std::stoi(comm_group_name);

        std::vector<int> involved_NPUs;
        for (auto id : it.value()) {
            involved_NPUs.push_back(id);
        }

        comm_groups[comm_group_id] = new CommunicatorGroup(comm_group_id, involved_NPUs, sys, sys->comm_NI);
    }
}

int Workload::extract_block_id_from_name(const std::string& name) {
    if (name.rfind("stack_", 0) == 0) { // Check if the name starts with "stack_"
        size_t start = 6; // Length of "stack_"
        size_t end = name.find('_', start);
        if (end != std::string::npos) {
            try {
                return std::stoi(name.substr(start, end - start));
            }
            catch (const std::exception&) {
                return -1; // Parsing failed
            }
        }
    }
    return -1; // Not a node within a transformer block
}

void Workload::issue_dep_free_nodes() {
    std::queue<shared_ptr<Chakra::ETFeederNode>> push_back_queue;
    shared_ptr<Chakra::ETFeederNode> node = et_feeder->getNextIssuableNode();
    while (node != nullptr) {
        if (hw_resource->is_available(node)) {
            issue(node);
        }
        else {
            push_back_queue.push(node);
        }
        node = et_feeder->getNextIssuableNode();
    }

    while (!push_back_queue.empty()) {
        shared_ptr<Chakra::ETFeederNode> node = push_back_queue.front();
        et_feeder->pushBackIssuableNode(node->id());
        push_back_queue.pop();
    }
}

void Workload::issue(shared_ptr<Chakra::ETFeederNode> node) {
    auto logger = LoggerFactory::get_logger("workload");

    // ==== BEGIN: Block-level Optimization Logic ====
    if (block_level_optimization_enabled) {
        int block_id = extract_block_id_from_name(node->name());

        // Build block mapping incrementally
        if (block_id != -1) {
            block_to_nodes_map[block_id].push_back(node->id());
        }

        // Timing logic for the first block
        if (block_id == 0) {
            if (first_block_ongoing_nodes.empty()) {
                // This is the very first node of the first block
                block_tracking_start_time = Sys::boostedTick();
                logger->info("Block-level Opt.: Detected start of the first block at tick {}", block_tracking_start_time);
            }
            // Add the node to the set of ongoing nodes
            first_block_ongoing_nodes.insert(node->id());
        }

        // Handle subsequent blocks after first block is measured
        if (block_id > 0 && first_block_latency > 0) {
            // Check if this block has been fast-forwarded
            if (fast_forwarded_blocks.find(block_id) == fast_forwarded_blocks.end()) {
                // First node of a new block - fast-forward entire block
                fast_forwarded_blocks.insert(block_id);
                current_block_id = block_id;
                logger->info("Block-level Opt.: Fast-forwarding block {}, latency = {} ns", block_id, first_block_latency);

                // Occupy the first node
                hw_resource->occupy(node);

                // Schedule a callback to handle the entire block
                WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
                wlhd->node_id = node->id();
                wlhd->block_id = block_id;
                sys->register_event(this, EventType::BlockFastForward, wlhd, first_block_latency);
                return;
            }
            else {
                // Subsequent node is an already fast-forwarded block - skip it
                hw_resource->occupy(node);
                return;
            }
        }
    }
    // ==== END: Block-level Optimization Logic ====

    // ==== Original issue logic ====
    if (sys->replay_only) {
        hw_resource->occupy(node);
        issue_replay(node);
    }
    else {
        if ((node->type() == ChakraNodeType::MEM_LOAD_NODE) ||
            (node->type() == ChakraNodeType::MEM_STORE_NODE)) {
            if (sys->trace_enabled) {
                logger->debug("issue,sys->id={}, tick={}, node->id={}, "
                    "node->name={}, node->type={}",
                    sys->id, Sys::boostedTick(), node->id(),
                    node->name(),
                    static_cast<uint64_t>(node->type()));
            }
            issue_remote_mem(node);
        }
        else if (node->is_cpu_op() || (!node->is_cpu_op() && node->type() == ChakraNodeType::COMP_NODE)) {
            if ((node->runtime() == 0) && (node->num_ops() == 0)) {
                skip_invalid(node);
            }
            else {
                if (sys->trace_enabled) {
                    logger->debug("issue,sys->id={}, tick={}, node->id={}, "
                        "node->name={}, node->type={}",
                        sys->id, Sys::boostedTick(), node->id(),
                        node->name(),
                        static_cast<uint64_t>(node->type()));
                }
                issue_comp(node);
            }
        }
        else if (!node->is_cpu_op() && (node->type() == ChakraNodeType::COMM_COLL_NODE || (node->type() == ChakraNodeType::COMM_SEND_NODE) || (node->type() == ChakraNodeType::COMM_RECV_NODE))) {
            if (sys->trace_enabled) {
                logger->debug("issue,sys->id={}, tick={}, node->id={}, "
                    "node->name={}, node->type={}",
                    sys->id, Sys::boostedTick(), node->id(),
                    node->name(),
                    static_cast<uint64_t>(node->type()));
            }
            issue_comm(node);
        }
        else if (node->type() == ChakraNodeType::INVALID_NODE) {
            skip_invalid(node);
        }
    }
}

void Workload::issue_replay(shared_ptr<Chakra::ETFeederNode> node) {
    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->node_id = node->id();
    uint64_t runtime = 1ul;
    if (node->runtime() != 0ul) {
        // chakra runtimes are in microseconds and we should convert it into
        // nanoseconds
        runtime = node->runtime() * 1000;
    }
    if (node->is_cpu_op()) {
        hw_resource->tics_cpu_ops += runtime;
    }
    else {
        hw_resource->tics_gpu_ops += runtime;
    }
    sys->register_event(this, EventType::General, wlhd, runtime);
}

void Workload::issue_remote_mem(shared_ptr<Chakra::ETFeederNode> node) {
    hw_resource->occupy(node);

    if (in_flight_ops_start_time.find(node->id()) == in_flight_ops_start_time.end()) {
        in_flight_ops_start_time[node->id()] = Sys::boostedTick();
    }

    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->sys_id = sys->id;
    wlhd->workload = this;
    wlhd->node_id = node->id();
    sys->remote_mem->issue(node->tensor_size(), wlhd);
}

void Workload::issue_comp(shared_ptr<Chakra::ETFeederNode> node) {
    hw_resource->occupy(node);
    uint64_t runtime = 1; // ns (default fallback)

    auto logger = LoggerFactory::get_logger("workload");

    // =========================================================================
    // COMPUTE NODE EXECUTION: ROOFLINE-ONLY MODE
    // =========================================================================
    if (sys->roofline_enabled && !sys->scalesim_enabled) {
        double num_ops = static_cast<double>(node->num_ops());
        double tensor_size = static_cast<double>(node->tensor_size());

        if (tensor_size == 0) {
            logger->warn("Node {} has zero tensor_size. Using num_ops as tensor_size.", node->name());
            tensor_size = num_ops;
        }

        double operational_intensity = num_ops / tensor_size;

        // Use roofline model with statistics logging
        double perf = sys->roofline->get_perf_with_stats(operational_intensity, num_ops);
        double elapsed_time = num_ops / perf;  // elapsed_time: sec; perf: FLOPS/s

        runtime = static_cast<uint64_t>(elapsed_time * 1e9);  // sec -> ns
        logger->info(
            "[Roofline] Node: {}, Ops: {}, OI: {:.4f}, Perf: {:.4f} GFLOPS, Runtime: {} cycles",
            node->name(), static_cast<uint64_t>(num_ops), operational_intensity, perf / 1e9, runtime
        );
    }
    // =========================================================================
    // COMPUTE NODE EXECUTION: SCALE-SIM MODE (with Roofline fallback)
    // =========================================================================
    else if (sys->scalesim_enabled) {
        double num_ops = static_cast<double>(node->num_ops());
        double tensor_size = static_cast<double>(node->tensor_size());

        bool is_matmul = false;
        if (node->has_other_attr("is_matmul")) {
            const auto& attr = node->get_other_attr("is_matmul");
            if (attr.value_case() == ChakraProtoMsg::AttributeProto::kBoolVal) {
                is_matmul = attr.bool_val();
            }
        }

        // ---------------------------------------------------------------------
        // CASE 1: MatMul Node - Use SCALE-Sim for detailed systolic array simulation
        // ---------------------------------------------------------------------
        if (is_matmul) {
            std::string csv_file = get_required_env_var("ASTRA_SCALESIM_LUT_PATH");

            // Update stats for this matmul node
            double operational_intensity = num_ops / tensor_size;
            sys->roofline->update_stats(operational_intensity, num_ops);

            // Extract M, N, K dimensions
            uint64_t M = 0, N = 0, K = 0;
            if (node->has_other_attr("M")) {
                const auto& attr = node->get_other_attr("M");
                if (attr.value_case() == ChakraProtoMsg::AttributeProto::kInt32Val) {
                    M = static_cast<uint64_t>(attr.int32_val());
                }
            }
            if (node->has_other_attr("N")) {
                const auto& attr = node->get_other_attr("N");
                if (attr.value_case() == ChakraProtoMsg::AttributeProto::kInt32Val) {
                    N = static_cast<uint64_t>(attr.int32_val());
                }
            }
            if (node->has_other_attr("K")) {
                const auto& attr = node->get_other_attr("K");
                if (attr.value_case() == ChakraProtoMsg::AttributeProto::kInt32Val) {
                    K = static_cast<uint64_t>(attr.int32_val());
                }
            }

            // Validate M, N, K dimensions
            if (M == 0 || N == 0 || K == 0) {
                logger->critical("[SCALE-Sim] ERROR: Matmul node {} has invalid dimensions M={}, N={}, K={}. Cannot run SCALE-Sim.", node->name(), M, N, K);
                exit(EXIT_FAILURE);
            }

            logger->info("[SCALE-Sim] Node: {}, Matmul dimensions: M={}, N={}, K={}", node->name(), M, N, K);

            // Get system hardware configuration
            uint64_t array_height = sys->array_height;
            uint64_t array_width = sys->array_width;
            uint64_t num_arrays = sys->num_arrays;
            uint64_t SRAM_size_kB = sys->sram_size;  // kB
            double total_DRAM_BW_GBps = sys->local_mem_bw / 1000000000; // GB/s

            logger->debug(
                "[SCALE-Sim] Hardware config: {}x{} arrays ({} arrays in total), SRAM: {} kB, DRAM BW: {:.2f} GB/s",
                array_height, array_width, num_arrays, SRAM_size_kB, total_DRAM_BW_GBps
            );

            // ==================================================================
            // STEP 1: 1D tiling along the larger dimension (M or N)
            // ==================================================================
            auto ceil_div = [](uint64_t a, uint64_t b) { return (a + b - 1) / b; };

            // Choose the larger dimension to tile along (common for LLM inference)
            bool tile_along_N = (N >= M);
            uint64_t major_dim = tile_along_N ? N : M;

            // Tiling knobs (const): target per-tile extent and hard cap on major dimension
            // First fully utilize arrays (p_major >= num_arrays), then trim the tile with kMaxMajorTile
            const uint64_t kTargetMajorTile = 512;   // aim for ~this many elements along the major dim per tile
            const uint64_t kMaxMajorTile = 2048;  // hard cap; if exceeded, we chunk serially via repeat_factor

            // Problem-driven tiling count along the major dimension
            uint64_t p_major_target = std::max<uint64_t>(1, ceil_div(major_dim, kTargetMajorTile));
            // Ensure all arrays can be occupied in each wave (if problem is large enough)
            uint64_t p_major = std::max<uint64_t>(p_major_target, std::max<uint64_t>(1, num_arrays));
            // Do not exceed the problem size
            p_major = std::min<uint64_t>(p_major, major_dim);

            // 1D tiling: allocate all partitions along the major dimension only
            uint64_t P_rows = tile_along_N ? 1 : p_major;
            uint64_t P_cols = tile_along_N ? p_major : 1;

            // Final partitions and arrays used in parallel
            uint64_t p_rows = P_rows;
            uint64_t p_cols = P_cols;
            uint64_t total_tiles = p_rows * p_cols;
            uint64_t arrays_used = std::min<uint64_t>(num_arrays, total_tiles);

            // Derive tile sizes (pre-trim)
            uint64_t best_tile_M = ceil_div(M, p_rows);
            uint64_t best_tile_N = ceil_div(N, p_cols);
            uint64_t best_tile_K = K;

            // ==================================================================
            // STEP 1.5: Cap tile extent on the major dimension and add repeats
            // ==================================================================
            uint64_t repeat_factor = 1;
            if (tile_along_N) {
                if (best_tile_N > kMaxMajorTile) {
                    repeat_factor = ceil_div(best_tile_N, kMaxMajorTile);
                    best_tile_N = ceil_div(best_tile_N, repeat_factor);
                    logger->info("[SCALE-Sim] Major-dim cap: N tile {} -> {} with repeats={}",
                        (uint64_t)ceil_div(N, p_cols), best_tile_N, repeat_factor);
                }
            }
            else {
                if (best_tile_M > kMaxMajorTile) {
                    repeat_factor = ceil_div(best_tile_M, kMaxMajorTile);
                    best_tile_M = ceil_div(best_tile_M, repeat_factor);
                    logger->info("[SCALE-Sim] Major-dim cap: M tile {} -> {} with repeats={}",
                        (uint64_t)ceil_div(M, p_rows), best_tile_M, repeat_factor);
                }
            }

            logger->info(
                "[SCALE-Sim] Tiling (1D on {}): {}x{} tiles (p_major={}, target={} max={}) (tile {}x{}x{}, arrays_used={}/{}).",
                tile_along_N ? "N" : "M",
                p_rows, p_cols, p_major, kTargetMajorTile, kMaxMajorTile,
                best_tile_M, best_tile_N, best_tile_K, arrays_used, num_arrays
            );

            // ==================================================================
            // STEP 2: Use tile dimensions
            // ==================================================================
            uint64_t tile_M = best_tile_M;
            uint64_t tile_N = best_tile_N;
            uint64_t tile_K = best_tile_K;

            // ==================================================================
            // STEP 3: Calculate per-array bandwidth and SRAM size (for this tiling)
            // ==================================================================
            uint64_t bandwidth_per_array_GBps = std::max<uint64_t>(1, (uint64_t)(total_DRAM_BW_GBps / std::max<uint64_t>(1, arrays_used)));
            uint64_t sram_size_per_array_kB = std::max<uint64_t>(1, (uint64_t)(SRAM_size_kB / std::max<uint64_t>(1, arrays_used)));

            logger->debug(
                "[SCALE-Sim] Per-array resources: BW={} GB/s, SRAM={} kB (arrays_used={})",
                bandwidth_per_array_GBps, sram_size_per_array_kB, arrays_used
            );

            // ==================================================================
            // STEP 4: Lookup or run Scale-Sim for the tile
            // ==================================================================
            double mapping_eff = 0.0, compute_util = 0.0, overall_util = 0.0;
            uint64_t tile_runtime_cycles = lookup_in_csv(
                csv_file, tile_M, tile_N, tile_K,
                sram_size_per_array_kB, bandwidth_per_array_GBps,
                array_height, array_width,
                &mapping_eff, &compute_util, &overall_util
            );

            if (tile_runtime_cycles > 0) {
                logger->info(
                    "[SCALE-Sim] Cache HIT: tile {}x{}x{} -> {} cycles (mapping_eff={:.4f}, compute_util={:.4f}, overall_util={:.4f})",
                    tile_M, tile_N, tile_K, tile_runtime_cycles,
                    mapping_eff, compute_util, overall_util
                );

                // Update SCALE-Sim statistics for cache hits
                sys->roofline->update_scalesim_stats(
                    mapping_eff,
                    compute_util,
                    overall_util,
                    static_cast<uint64_t>(num_ops)
                );
            }
            else {
                logger->info(
                    "[SCALE-Sim] Cache MISS: Running simulation for tile {}x{}x{}",
                    tile_M, tile_N, tile_K
                );

                bool scalesim_success = false;
                uint64_t tile_compute_cycles = 0;

                tile_runtime_cycles = run_scalesim_for_tile(
                    tile_M, tile_N, tile_K, scalesim_success,
                    array_height, array_width,
                    sram_size_per_array_kB, bandwidth_per_array_GBps,
                    &tile_compute_cycles, &overall_util, &mapping_eff, &compute_util
                );
                if (scalesim_success && tile_runtime_cycles > 0) {
                    logger->info(
                        "[SCALE-Sim] Simulation SUCCESS: {} cycles (mapping_eff={:.4f}, compute_util={:.4f}, overall_util={:.4f})",
                        tile_runtime_cycles,
                        mapping_eff, compute_util, overall_util
                    );

                    // Update SCALE-Sim statistics in Roofline
                    sys->roofline->update_scalesim_stats(
                        mapping_eff,
                        compute_util,
                        overall_util,
                        static_cast<uint64_t>(num_ops)
                    );

                    // Cache the result for future lookups
                    append_to_lookup_csv(
                        csv_file, tile_M, tile_N, tile_K,
                        array_height, array_width,
                        sram_size_per_array_kB, bandwidth_per_array_GBps,
                        tile_runtime_cycles, tile_compute_cycles,
                        mapping_eff, compute_util, overall_util
                    );

                    logger->debug("[SCALE-Sim] Result cached to: {}", csv_file);
                }
                else {
                    logger->critical(
                        "[SCALE-Sim] Simulation FAILED for tile M={}, N={}, K={}",
                        tile_M, tile_N, tile_K
                    );
                    logger->critical("[SCALE-Sim] Check SCALE-Sim installation and configuration.");
                    exit(EXIT_FAILURE);
                }
            }

            uint64_t waves = (num_arrays == 0) ? 1 : ((total_tiles + num_arrays - 1) / num_arrays);
            uint64_t est_total_runtime = waves * tile_runtime_cycles;
            est_total_runtime *= std::max<uint64_t>(1, repeat_factor);

            logger->info(
                "[SCALE-Sim] Estimated total runtime with waves={} repeats={} -> {} cycles (per-tile {} cycles)",
                waves, repeat_factor, est_total_runtime, tile_runtime_cycles
            );
            runtime = est_total_runtime;
        }

        // ---------------------------------------------------------------------
        // CASE 2: Non-MatMul Node - Fall back to Roofline model
        // ---------------------------------------------------------------------
        else {
            // Is not matmul node
            if (!sys->roofline_enabled) {
                logger->critical("[SCALE-Sim] ERROR: Node {} is not a MatMul, but Roofline fallback is disabled!", node->name());
                exit(EXIT_FAILURE);
            }
            if (tensor_size == 0) {
                logger->warn("[Roofline] Node {} has zero tensor_size. Using num_ops as tensor_size.", node->name());
                tensor_size = num_ops;
            }

            double operational_intensity = num_ops / tensor_size;
            double perf = sys->roofline->get_perf_with_stats(operational_intensity, num_ops);
            double elapsed_time = num_ops / perf;

            runtime = static_cast<uint64_t>(elapsed_time * 1e9);

            logger->info(
                "[Roofline] Node: {}, Ops: {}, OI: {:.4f}, Perf: {:.4f} GFLOPS, Runtime: {} cycles",
                node->name(), static_cast<uint64_t>(num_ops), operational_intensity, perf / 1e9, runtime
            );
        }
    }
    // =========================================================================
    // CRITICAL ERROR: Both models disabled
    // =========================================================================
    else {
        logger->critical("[ERROR] Both Roofline and SCALE-Sim are disabled!");
        logger->critical("[ERROR] Set 'roofline-enabled' or 'scalesim-enabled' in system config.");
        exit(EXIT_FAILURE);

        logger->warn("Using node runtime.");
        // advance this node forward the recorded "replayed" time specificed in the ET.
        issue_replay(node);
    }

    // Unified event registration and resource management
    WorkloadLayerHandlerData* wlhd = new WorkloadLayerHandlerData;
    wlhd->node_id = node->id();

    if (node->is_cpu_op()) {
        hw_resource->tics_cpu_ops += runtime;
    }
    else {
        hw_resource->tics_gpu_ops += runtime;
    }
    sys->register_event(this, EventType::General, wlhd, runtime);
}

void Workload::issue_comm(shared_ptr<Chakra::ETFeederNode> node) {
    // Print timing info when starting communication node
    // std::cout << "[TIMING] issue_comm START: sys_id=" << sys->id
    //     << ", tick=" << Sys::boostedTick()
    //     << ", node_id=" << node->id()
    //     << ", node_name=" << node->name()
    //     << ", comm_type=" << static_cast<uint64_t>(node->comm_type())
    //     << ", comm_size=" << node->comm_size() << std::endl;

    hw_resource->occupy(node);

    vector<bool> involved_dim;

    if (node->has_other_attr("involved_dim")) {
        const ChakraProtoMsg::AttributeProto& attr =
            node->get_other_attr("involved_dim");

        // Ensure the attribute is of type bool_list before accessing
        if (attr.has_bool_list()) {
            const ChakraProtoMsg::BoolList& bool_list = attr.bool_list();

            // Traverse bool_list and add values to involved_dim
            for (int i = 0; i < bool_list.values_size(); ++i) {
                involved_dim.push_back(bool_list.values(i));
            }
        }
        else {
            cerr << "Expected bool_list in involved_dim but found another type."
                << endl;
            exit(EXIT_FAILURE);
        }
    }
    else {
        // involved_dim does not exist in ETFeeder.
        // Assume involved_dim = [1,1,1,1,1] which we could simulate 5-Dimension.
        // Could use Process Group to build involved_dim later. 
        // Once process group is implemented, you should get
        // that with node->pg_name()

        for (int i = 0; i < 4; i++)
            involved_dim.push_back(true);
    }

    CommunicatorGroup* comm_group = extract_comm_group(node);

    if (!node->is_cpu_op() && (node->type() == ChakraNodeType::COMM_COLL_NODE)) {
        if (node->comm_type() == ChakraCollectiveCommType::ALL_REDUCE) {
            DataSet* fp =
                sys->generate_all_reduce(node->comm_size(), involved_dim,
                    comm_group, node->comm_priority());
            collective_comm_node_id_map[fp->my_id] = node->id();
            collective_comm_wrapper_map[fp->my_id] = fp;
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

        }
        else if (node->comm_type() == ChakraCollectiveCommType::ALL_TO_ALL) {
            DataSet* fp =
                sys->generate_all_to_all(node->comm_size(), involved_dim,
                    comm_group, node->comm_priority());
            collective_comm_node_id_map[fp->my_id] = node->id();
            collective_comm_wrapper_map[fp->my_id] = fp;
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

        }
        else if (node->comm_type() == ChakraCollectiveCommType::ALL_GATHER) {
            DataSet* fp =
                sys->generate_all_gather(node->comm_size(), involved_dim,
                    comm_group, node->comm_priority());
            collective_comm_node_id_map[fp->my_id] = node->id();
            collective_comm_wrapper_map[fp->my_id] = fp;
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

        }
        else if (node->comm_type() ==
            ChakraCollectiveCommType::REDUCE_SCATTER) {
            DataSet* fp =
                sys->generate_reduce_scatter(node->comm_size(), involved_dim,
                    comm_group, node->comm_priority());
            collective_comm_node_id_map[fp->my_id] = node->id();
            collective_comm_wrapper_map[fp->my_id] = fp;
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);

        }
        else if (node->comm_type() == ChakraCollectiveCommType::BROADCAST) {
            // broadcast colelctive has not been implemented in ASTRA-SIM yet.
            // So, we just use its real system mesurements
            uint64_t runtime = 1ul;
            if (node->runtime() != 0ul) {
                // chakra runtimes are in microseconds and we should convert it
                // into nanoseconds
                runtime = node->runtime() * 1000;
            }
            DataSet* fp = new DataSet(1);
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);
            collective_comm_node_id_map[fp->my_id] = node->id();
            collective_comm_wrapper_map[fp->my_id] = fp;
            sys->register_event(fp, EventType::General, nullptr,
                // chakra runtimes are in microseconds and we
                // should convert it into nanoseconds
                runtime);
            fp->set_notifier(this, EventType::CollectiveCommunicationFinished);
        }
    }
    else if (node->type() == ChakraNodeType::COMM_SEND_NODE) {
        // Only time the SEND operation as it represents the active part of P2P
        if (in_flight_ops_start_time.find(node->id()) == in_flight_ops_start_time.end()) {
            in_flight_ops_start_time[node->id()] = Sys::boostedTick();
        }
        sim_request snd_req;
        snd_req.srcRank = node->comm_src();
        snd_req.dstRank = node->comm_dst();
        snd_req.reqType = UINT8;
        SendPacketEventHandlerData* sehd = new SendPacketEventHandlerData;
        sehd->callable = this;
        sehd->wlhd = new WorkloadLayerHandlerData;
        sehd->wlhd->node_id = node->id();
        sehd->event = EventType::PacketSent;
        sys->front_end_sim_send(0, Sys::dummy_data, node->comm_size(), UINT8,
            node->comm_dst(), node->comm_tag(), &snd_req,
            Sys::FrontEndSendRecvType::NATIVE,
            &Sys::handleEvent, sehd);
    }
    else if (node->type() == ChakraNodeType::COMM_RECV_NODE) {
        // A RECV node is just a placeholder for dependency, not a timed operation itself.
        sim_request rcv_req;
        RecvPacketEventHandlerData* rcehd = new RecvPacketEventHandlerData;
        rcehd->wlhd = new WorkloadLayerHandlerData;
        rcehd->wlhd->node_id = node->id();
        rcehd->workload = this;
        rcehd->event = EventType::PacketReceived;
        sys->front_end_sim_recv(0, Sys::dummy_data, node->comm_size(), UINT8,
            node->comm_src(), node->comm_tag(), &rcv_req,
            Sys::FrontEndSendRecvType::NATIVE,
            &Sys::handleEvent, rcehd);
    }
    else {
        LoggerFactory::get_logger("workload")
            ->critical("Unknown communication node type");
        exit(EXIT_FAILURE);
    }
}

void Workload::skip_invalid(shared_ptr<Chakra::ETFeederNode> node) {
    et_feeder->freeChildrenNodes(node->id());
    et_feeder->removeNode(node->id());
}

void Workload::call(EventType event, CallData* data) {
    if (is_finished) {
        return;
    }

    uint64_t finished_node_id = 0;
    if (event == EventType::BlockFastForward) {
        // Sanity check: block-level optimization is enabled
        if (!block_level_optimization_enabled) {
            LoggerFactory::get_logger("workload")
                ->error("Received BlockFastForward event but block-level optimization is disabled.");
            return;
        }
        WorkloadLayerHandlerData* wlhd = (WorkloadLayerHandlerData*)data;
        int block_id = wlhd->block_id;

        auto logger = LoggerFactory::get_logger("workload");
        logger->info("Block-level Opt.: Finished fast-forwarding block {} at tick {}", block_id, Sys::boostedTick());

        // Free all nodes in this block
        if (block_to_nodes_map.find(block_id) != block_to_nodes_map.end()) {
            std::vector<uint64_t> nodes_in_block = block_to_nodes_map[block_id];

            // First pass: Release all nodes and collect their children
            std::unordered_set<uint64_t> all_children_to_free;
            for (uint64_t node_id : nodes_in_block) {
                try {
                    shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(node_id);
                    hw_resource->release(node);
                    for (auto child : node->getChildren()) {
                        all_children_to_free.insert(child->getChakraNode()->id());
                    }
                }
                catch (const std::out_of_range&) {
                    // Node not found - might have been processed already
                    logger->warn("Block-level Opt.: Node {} not found in dep_graph during block {} cleanup",
                        node_id, block_id);
                    continue;
                }
            }

            // Second pass: Free children dependencies
            for (uint64_t node_id : nodes_in_block) {
                try {
                    shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(node_id);

                    // Free this node's children (decrement their dependency count)
                    for (auto child : node->getChildren()) {
                        auto child_chakra = child->getChakraNode();

                        // Remove this node from child's dependencies
                        for (auto it = child_chakra->mutable_data_deps()->begin();
                            it != child_chakra->mutable_data_deps()->end();
                            ++it) {
                            if (*it == node_id) {
                                child_chakra->mutable_data_deps()->erase(it);
                                break;
                            }
                        }

                        // If child has no more dependencies, add to issuable queue
                        if (child_chakra->data_deps().size() == 0) {
                            et_feeder->pushBackIssuableNode(child->getChakraNode()->id());
                        }
                    }
                }
                catch (const std::out_of_range&) {
                    continue;
                }
            }

            // Third pass: Remove all nodes from the dependency graph
            for (uint64_t node_id : nodes_in_block) {
                try {
                    et_feeder->removeNode(node_id);
                }
                catch (const std::out_of_range&) {
                    // Already removed, skip
                    continue;
                }
            }

            // Clear the block mapping since it's been processed
            block_to_nodes_map.erase(block_id);

            logger->info("Block-level Opt.: Processed {} nodes in block {}",
                nodes_in_block.size(), block_id);

            // Clear the block mapping since it's been processed
            block_to_nodes_map.erase(block_id);
        }

        issue_dep_free_nodes();
        delete wlhd; // Clean up the handler data
        return;
    }

    if (event == EventType::CollectiveCommunicationFinished) {
        IntData* int_data = (IntData*)data;
        uint64_t coll_comm_id = int_data->data;

        hw_resource->tics_gpu_comms += int_data->execution_time;
        uint64_t node_id = collective_comm_node_id_map[coll_comm_id];
        shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(node_id);

        // Print timing info when collective communication finishes
        // std::cout << "[TIMING] CollectiveCommunicationFinished: sys_id=" << sys->id
        //     << ", tick=" << Sys::boostedTick()
        //     << ", node_id=" << node->id()
        //     << ", node_name=" << node->name()
        //     << ", execution_time=" << int_data->execution_time << std::endl;

        if (sys->trace_enabled) {
            LoggerFactory::get_logger("workload")
                ->debug("callback,sys->id={}, tick={}, node->id={}, "
                    "node->name={}, node->type={}",
                    sys->id, Sys::boostedTick(), node->id(), node->name(),
                    static_cast<uint64_t>(node->type()));
        }

        hw_resource->release(node);
        et_feeder->freeChildrenNodes(node_id);
        issue_dep_free_nodes();

        // The Dataset class provides statistics that should be used later to dump
        // more statistics in the workload layer
        delete collective_comm_wrapper_map[coll_comm_id];
        collective_comm_wrapper_map.erase(coll_comm_id);
        et_feeder->removeNode(node_id);

    }
    else {
        if (data == nullptr) {
            issue_dep_free_nodes();
        }
        else {
            WorkloadLayerHandlerData* wlhd = (WorkloadLayerHandlerData*)data;
            finished_node_id = wlhd->node_id;
            shared_ptr<Chakra::ETFeederNode> node = et_feeder->lookupNode(wlhd->node_id);

            // Print timing info when general node finishes
            // std::cout << "[TIMING] GeneralNodeFinished: sys_id=" << sys->id
            //     << ", tick=" << Sys::boostedTick()
            //     << ", node_id=" << node->id()
            //     << ", node_name=" << node->name()
            //     << ", node_type=" << static_cast<uint64_t>(node->type()) << std::endl;

            if (in_flight_ops_start_time.count(node->id())) {
                Tick start_time = in_flight_ops_start_time[node->id()];
                Tick end_time = Sys::boostedTick();
                Tick duration = end_time - start_time;

                if (node->type() == ChakraNodeType::MEM_LOAD_NODE ||
                    node->type() == ChakraNodeType::MEM_STORE_NODE) {
                    hw_resource->tics_gpu_remote_mem += duration;
                }
                else if (node->type() == ChakraNodeType::COMM_SEND_NODE) {
                    // Only SEND operation's duration is added to P2P time
                    hw_resource->tics_gpu_p2p_comms += duration;
                }
                in_flight_ops_start_time.erase(node->id());
            }

            if (sys->trace_enabled) {
                LoggerFactory::get_logger("workload")
                    ->debug("callback,sys->id={}, tick={}, node->id={}, "
                        "node->name={}, node->type={}",
                        sys->id, Sys::boostedTick(), node->id(),
                        node->name(), static_cast<uint64_t>(node->type()));
            }

            hw_resource->release(node);
            et_feeder->freeChildrenNodes(node->id());
            issue_dep_free_nodes();
            et_feeder->removeNode(wlhd->node_id);
            // DO NOT delete wlhd here. The owner of wlhd (e.g., SendPacketEventHandlerData)
            // is responsible for deleting it. Deleting it here causes a double-free error.
        }
    }

    // ==== BEGIN: Block-level Optimization Logic ====
    if (block_level_optimization_enabled && first_block_latency == 0 && finished_node_id != 0) {
        if (first_block_ongoing_nodes.count(finished_node_id)) {
            first_block_ongoing_nodes.erase(finished_node_id);
            if (first_block_ongoing_nodes.empty()) {
                // This is the last node of the first block
                // All nodes in the first block have finished
                Tick block_end_time = Sys::boostedTick();
                first_block_latency = block_end_time - block_tracking_start_time;
                auto logger = LoggerFactory::get_logger("workload");
                logger->info("Block-level Opt.: Measured first block latency = {} ns", first_block_latency);
            }
        }
    }
    // ==== END: Block-level Optimization Logic ====

    if (!et_feeder->hasNodesToIssue() &&
        (hw_resource->num_in_flight_cpu_ops == 0) &&
        (hw_resource->num_in_flight_gpu_comp_ops == 0) &&
        (hw_resource->num_in_flight_gpu_comm_ops == 0)) {
        report();
        sys->comm_NI->sim_notify_finished();
        is_finished = true;
    }
}

void Workload::fire() {
    call(EventType::General, NULL);
}

void Workload::report() {
    Tick curr_tick = Sys::boostedTick();

    // Final adjustment for idle time: account for the time from the last busy state to the end of the simulation
    if (!hw_resource->is_gpu_busy()) {
        hw_resource->tics_gpu_idle += (curr_tick - hw_resource->last_gpu_busy_finish_time);
    }

    LoggerFactory::get_logger("workload")
        ->info("[Summary] sys[{}] finished.", sys->id);
    LoggerFactory::get_logger("workload")
        ->info("    total_cycles:         {}", curr_tick);
    LoggerFactory::get_logger("workload")
        ->info("    total_gpu_comp:       {}", hw_resource->tics_gpu_ops);
    LoggerFactory::get_logger("workload")
        ->info("    total_gpu_coll_comm:  {}", hw_resource->tics_gpu_comms);
    LoggerFactory::get_logger("workload")
        ->info("    total_gpu_p2p_comm:   {}", hw_resource->tics_gpu_p2p_comms);
    LoggerFactory::get_logger("workload")
        ->info("    total_gpu_mem:        {}", hw_resource->tics_gpu_remote_mem);
    LoggerFactory::get_logger("workload")
        ->info("    total_cpu_comp:       {}", hw_resource->tics_cpu_ops);
    LoggerFactory::get_logger("workload")
        ->info("    total_gpu_idle_time:  {}", hw_resource->tics_gpu_idle);
}

nlohmann::json Workload::get_summary_json() {
    Tick curr_tick = Sys::boostedTick();

    // Final adjustment for idle time: account for the time from the last busy state to the end of the simulation
    if (!hw_resource->is_gpu_busy()) {
        hw_resource->tics_gpu_idle += (curr_tick - hw_resource->last_gpu_busy_finish_time);
    }

    // Print roofline stats if enabled
    if (sys->roofline_enabled) {
        std::cout << "\n";
        sys->roofline->print_roofline_stats();
        if (sys->scalesim_enabled) {
            sys->roofline->print_scalesim_stats();
        }
        std::cout << "\n";
    }

    nlohmann::json summary;
    summary["total_cycles"] = curr_tick;
    summary["total_gpu_comp"] = hw_resource->tics_gpu_ops;
    summary["total_gpu_coll_comm"] = hw_resource->tics_gpu_comms;
    summary["total_gpu_p2p_comm"] = hw_resource->tics_gpu_p2p_comms;
    summary["total_gpu_idle_time"] = hw_resource->tics_gpu_idle;

    summary["roofline_enabled"] = false;
    summary["scalesim_enabled"] = false;
    if (sys->roofline_enabled) {
        summary["roofline_enabled"] = true;
        summary["roofline_summary"] = {};
        nlohmann::json& roofline_summary = summary["roofline_summary"];
        roofline_summary["peak_perf_tflops"] = sys->roofline->get_peak_perf() / 1e12;
        roofline_summary["memory_bandwidth_gbps"] = sys->roofline->get_bandwidth() / 1e9;
        roofline_summary["total_operations"] = sys->roofline->get_total_ops();
        roofline_summary["total_roofline_calls"] = sys->roofline->get_total_calls();
        roofline_summary["weighted_avg_operational_intensity"] = sys->roofline->get_weighted_avg_oi();
        roofline_summary["bandwidth_limited_ops_ratio"] = sys->roofline->get_bandwidth_limited_ratio();
        roofline_summary["compute_limited_ops_ratio"] = sys->roofline->get_compute_limited_ratio();

        // Calculate bandwidth performance vs peak performance ratio
        double weighted_avg_oi = sys->roofline->get_weighted_avg_oi();
        double bandwidth_perf_peak_ratio = (sys->roofline->get_bandwidth() * weighted_avg_oi) / sys->roofline->get_peak_perf();
        roofline_summary["bandwidth_perf_peak_ratio"] = bandwidth_perf_peak_ratio;
        roofline_summary["overall_bottleneck"] = (bandwidth_perf_peak_ratio < 1.0) ? "bandwidth" : "compute";
    }
    if (sys->scalesim_enabled) {
        summary["scalesim_enabled"] = true;
        summary["scalesim_summary"] = {};
        nlohmann::json& scalesim_summary = summary["scalesim_summary"];
        scalesim_summary["total_matmul_operations"] = sys->roofline->get_scalesim_total_ops();
        scalesim_summary["total_scalesim_operations"] = sys->roofline->get_scalesim_total_matmuls();
        scalesim_summary["weighted_avg_mapping_efficiency"] = sys->roofline->get_scalesim_weighted_avg_mapping_eff();
        scalesim_summary["weighted_avg_compute_utilization"] = sys->roofline->get_scalesim_weighted_avg_compute_util();
        scalesim_summary["weighted_avg_overall_utilization"] = sys->roofline->get_scalesim_weighted_avg_overall_util();
    }

    return summary;
}

CommunicatorGroup* Workload::extract_comm_group(std::shared_ptr<Chakra::ETFeederNode> node) {
    std::string comm_group_name = node->pg_name();
    if (comm_group_name == "") {
        // No communicator group is specified for this communication ET node.
        return nullptr;
    }

    int comm_group_id = std::stoi(comm_group_name);
    if (comm_groups.find(comm_group_id) == comm_groups.end()) {
        LoggerFactory::get_logger("workload")
            ->critical("For rank {} ET node {}, communicator group {} not found", sys->id, node->id(), comm_group_id);
        exit(EXIT_FAILURE);
    }
    return comm_groups[comm_group_id];
}
