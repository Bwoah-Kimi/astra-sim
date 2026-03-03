
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <tuple>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <cmath>
#include <cstdlib>

#include "cxxopts.hpp"

// 包含 Workload.hh 来引入所有必要的类型和命名空间定义
// #include "astra-sim/workload/Workload.hh" 



#include "extern/graph_frontend/chakra/src/feeder/et_feeder.h"
#include "extern/graph_frontend/chakra/schema/protobuf/et_def.pb.h"

// 定义任务元组的类型别名，方便使用
using ComputeTask = std::tuple<uint64_t, uint64_t, uint64_t, int>; // M, N, K, Bandwidth

typedef ChakraProtoMsg::NodeType ChakraNodeType;

uint64_t get_env_var(const std::string& env_var_name, uint64_t default_val) {
    const char* value_str = std::getenv(env_var_name.c_str());
    if (value_str == nullptr) {
        // Environment variable not set, return default
        return default_val;
    }
    try {
        // Attempt to convert the string to a number
        return std::stoull(value_str);
    }
    catch (const std::exception& e) {
        // Conversion failed, return default
        std::cerr << "Warning: Could not parse environment variable '" << env_var_name << "'. Using default value: " << default_val << std::endl;
        return default_val;
    }
}

// ===================================================================
// Helper Functions (从 Workload.cpp 中移植并修改)
// ===================================================================

/**
 * @brief 创建 Scale-Sim 的配置文件 (.cfg)
 *
 * @param config_path 文件输出路径
 * @param array_height 计算阵列高度
 * @param array_width 计算阵列宽度
 * @param bandwidth DRAM 带宽 (GB/s, 但在 Scale-Sim 中通常解释为周期内元素数)
 * @param dataflow 数据流策略 (e.g., "os", "ws", "is")
 * @param run_name 运行的唯一名称
 */
 // in tools/cache_generator.cpp

void create_scalesim_config(
    const std::string& config_path,
    int array_height,
    int array_width,
    int bandwidth,
    const std::string& dataflow,
    const std::string& run_name) {

    const uint64_t ARRAY_DIM = get_env_var("ASTRASIM_ARRAY_DIM", 256);
    const uint64_t ifmap_sram_kb = get_env_var("SCALESIM_IFMAP_SRAM", 6144);
    const uint64_t filter_sram_kb = get_env_var("SCALESIM_FILTER_SRAM", 6144);
    const uint64_t ofmap_sram_kb = get_env_var("SCALESIM_OFMAP_SRAM", 2048);

    std::ofstream config_file(config_path);
    if (!config_file.is_open()) {
        throw std::runtime_error("Failed to create Scale-Sim config file at: " + config_path);
    }

    config_file << "[general]" << std::endl;
    config_file << "run_name = " << run_name << std::endl << std::endl;

    config_file << "[architecture_presets]" << std::endl;
    config_file << "ArrayHeight:    " << ARRAY_DIM << std::endl;
    config_file << "ArrayWidth:     " << ARRAY_DIM << std::endl;
    config_file << "IfmapSramSzkB:    " << ifmap_sram_kb << std::endl;
    config_file << "FilterSramSzkB:   " << filter_sram_kb << std::endl;
    config_file << "OfmapSramSzkB:    " << ofmap_sram_kb << std::endl;
    config_file << "IfmapOffset:    0" << std::endl;
    config_file << "FilterOffset:   10000000" << std::endl;
    config_file << "OfmapOffset:    20000000" << std::endl;
    config_file << "Dataflow : " << dataflow << std::endl;
    config_file << "Bandwidth : " << bandwidth << std::endl;
    config_file << "ReadRequestBuffer: 512" << std::endl;
    config_file << "WriteRequestBuffer: 512" << std::endl << std::endl;

    config_file << "[layout]" << std::endl;
    config_file << "IfmapCustomLayout: True" << std::endl;
    config_file << "IfmapSRAMBankBandwidth: 10" << std::endl;
    config_file << "IfmapSRAMBankNum: 10" << std::endl;
    config_file << "IfmapSRAMBankPort: 2" << std::endl;
    config_file << "FilterCustomLayout: True" << std::endl;
    config_file << "FilterSRAMBankBandwidth: 10" << std::endl;
    config_file << "FilterSRAMBankNum: 10" << std::endl;
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

/**
 * @brief 创建 Scale-Sim 的拓扑文件 (.csv)
 *
 * @param topo_path 文件输出路径
 * @param M 矩阵乘法维度 M
 * @param N 矩阵乘法维度 N
 * @param K 矩阵乘法维度 K
 */
void create_scalesim_topology(
    const std::string& topo_path,
    uint64_t M,
    uint64_t N,
    uint64_t K) {
    std::ofstream topo_file(topo_path);
    if (!topo_file.is_open()) {
        throw std::runtime_error("Failed to create Scale-Sim topology file at: " + topo_path);
    }
    topo_file << "Layer,M,N,K," << std::endl;
    topo_file << "matmul," << M << "," << N << "," << K << "," << std::endl;
    topo_file.close();
}

/**
 * @brief 从 Scale-Sim 结果文件中解析总周期数
 *
 * @param result_path 结果文件路径 (COMPUTE_REPORT.csv)
 * @return uint64_t 总周期数，失败则返回 0
 */
uint64_t parse_scalesim_result(const std::string& result_path) {
    std::ifstream result_file(result_path);
    if (!result_file.is_open()) {
        std::cerr << "Error: Could not open Scale-Sim result file: " << result_path << std::endl;
        return 0;
    }

    std::string header;
    if (!getline(result_file, header)) return 0;

    std::stringstream header_ss(header);
    std::vector<std::string> columns;
    std::string segment;
    while (std::getline(header_ss, segment, ',')) {
        columns.push_back(segment);
    }

    int cycle_col = -1;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].find("Total Cycles") != std::string::npos) {
            cycle_col = i;
            break;
        }
    }

    if (cycle_col == -1) {
        std::cerr << "Error: Could not find 'Total Cycles' in Scale-Sim report: " << result_path << std::endl;
        return 0;
    }

    std::string data_line;
    if (!getline(result_file, data_line)) return 0;

    std::stringstream data_ss(data_line);
    std::string value;
    for (int i = 0; i <= cycle_col; ++i) {
        if (!getline(data_ss, value, ',')) return 0;
    }

    try {
        return std::stoull(value);
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing cycle value '" << value << "': " << e.what() << std::endl;
        return 0;
    }
}


/**
 * @brief 读取现有的 CSV 缓存文件，加载已计算的任务
 *
 * @param cache_file_path CSV 文件路径
 * @return std::set<ComputeTask> 包含已计算任务的集合
 */
std::set<ComputeTask> load_existing_cache(const std::string& cache_file_path) {
    std::set<ComputeTask> existing_tasks;
    std::ifstream cache_file(cache_file_path);

    if (!cache_file.is_open()) {
        std::cout << "Info: Cache file '" << cache_file_path << "' not found. A new one will be created." << std::endl;
        return existing_tasks;
    }

    std::string line;
    // (可选) 跳过标题行
    // getline(cache_file, line); 

    while (getline(cache_file, line)) {
        std::stringstream ss(line);
        std::string m_str, n_str, k_str, bw_str, runtime_str;

        getline(ss, m_str, ',');
        getline(ss, n_str, ',');
        getline(ss, k_str, ',');
        getline(ss, bw_str, ',');
        getline(ss, runtime_str, ',');

        if (m_str.empty() || n_str.empty() || k_str.empty() || bw_str.empty()) {
            continue; // 跳过格式不正确的行
        }

        try {
            uint64_t m = std::stoull(m_str);
            uint64_t n = std::stoull(n_str);
            uint64_t k = std::stoull(k_str);
            int bw = std::stoi(bw_str);
            existing_tasks.insert({ m, n, k, bw });
        }
        catch (const std::exception& e) {
            // 静默忽略无法解析的行
        }
    }

    std::cout << "Info: Loaded " << existing_tasks.size() << " existing entries from cache." << std::endl;
    return existing_tasks;
}

uint64_t find_largest_factor_le(uint64_t n, uint64_t limit) {
    if (limit > n) {
        limit = n;
    }
    for (uint64_t i = limit; i >= 1; --i) {
        if (n % i == 0) {
            return i;
        }
    }
    return 1; // Should not happen if n > 0
}

// ===================================================================
// Main Logic
// ===================================================================

int main(int argc, char* argv[]) {
    // ---------------------------------------------------------------
    // 1. Parse Command Line Arguments
    // ---------------------------------------------------------------
    cxxopts::Options options("cache_generator", "A pre-computation tool for Astra-Sim to populate the Scale-Sim cache.");
    options.add_options()
        ("e,et_files", "List of ET files to scan (comma-separated)", cxxopts::value<std::string>())
        ("c,num_cores", "Number of parallel cores on the chiplet", cxxopts::value<int>()->default_value("16"))
        ("b,total_bw", "Total DRAM bandwidth for the chiplet", cxxopts::value<int>()->default_value("100"))
        ("o,output_script", "Path to the output shell script for Scale-Sim commands", cxxopts::value<std::string>()->default_value("run_scalesim.sh"))
        ("f,cache_file", "Path to the CSV cache file", cxxopts::value<std::string>()->default_value("precomputed_results.csv"))
        ("s,scalesim_path", "Path to the root of SCALE-Sim repository", cxxopts::value<std::string>()->default_value("/root/SCALE-Sim"))
        ("t,temp_dir", "Directory for temporary Scale-Sim files", cxxopts::value<std::string>()->default_value("/tmp/scalesim_temp"))
        ("w,workspace", "Workspace directory for Scale-Sim project outputs", cxxopts::value<std::string>())
        ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    std::string scalesim_workspace;
    if (result.count("workspace")) {
        scalesim_workspace = result["workspace"].as<std::string>();
    }
    else {
        // 如果没有提供 workspace，就使用旧的 temp_dir 行为作为备用
        scalesim_workspace = result["temp_dir"].as<std::string>();
    }
    const std::string work_dir = scalesim_workspace;

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("et_files")) {
        std::cerr << "Error: --et_files argument is required." << std::endl;
        std::cerr << options.help() << std::endl;
        return 1;
    }

    // Extract arguments
    std::string et_files_str = result["et_files"].as<std::string>();
    // const int NUM_CORES = result["num_cores"].as<int>();
    // const int TOTAL_DRAM_BANDWIDTH = result["total_bw"].as<int>();
    const std::string output_script_path = result["output_script"].as<std::string>();
    const std::string cache_file_path = result["cache_file"].as<std::string>();
    const std::string scalesim_root = result["scalesim_path"].as<std::string>();
    const std::string temp_dir = result["temp_dir"].as<std::string>();

    // Split comma-separated ET files into a vector
    std::vector<std::string> et_files;
    std::stringstream ss(et_files_str);
    std::string item;
    while (getline(ss, item, ',')) {
        et_files.push_back(item);
    }

    // ---------------------------------------------------------------
    // 2. Collect All Unique Compute Tasks
    // ---------------------------------------------------------------
    std::cout << "### Step 1: Scanning ET files to collect compute tasks..." << std::endl;
    std::set<ComputeTask> all_tasks;

    const uint64_t NUM_CORES = get_env_var("ASTRASIM_NUM_CORES", 16);
    const int TOTAL_DRAM_BANDWIDTH = get_env_var("ASTRASIN_TOTAL_DRAM_BANDWIDTH", 500);
    const uint64_t ARRAY_DIM = get_env_var("ASTRASIM_ARRAY_DIM", 256);

    for (const auto& et_path : et_files) {
        if (!std::filesystem::exists(et_path)) {
            std::cerr << "Warning: ET file not found, skipping: " << et_path << std::endl;
            continue;
        }
        std::cout << " -> Processing " << et_path << "..." << std::endl;
        try {
            // Use AstraSim's ETFeeder to parse the file
            Chakra::ETFeeder feeder(et_path);
            std::vector<Chakra::FeederV3::NodeId> all_ids = feeder.getAllNodeIds();

            for (const auto& node_id : all_ids) {
                std::shared_ptr<Chakra::FeederV3::ETFeederNode> node = feeder.lookupNode(node_id);
                if (node && node->type() == ChakraNodeType::COMP_NODE &&
                    node->has_attr("M") && node->has_attr("N") && node->has_attr("K")) {

                    uint64_t M = node->get_attr<uint64_t>("M");
                    uint64_t N = node->get_attr<uint64_t>("N");
                    uint64_t K = node->get_attr<uint64_t>("K");

                    // Apply the same sharding logic as in Workload::issue_comp
                    uint64_t P_rows = 1, P_cols = 1;

                    uint64_t ideal_p_cols = 1;
                    if (N > ARRAY_DIM) {
                        ideal_p_cols = static_cast<uint64_t>(std::floor(static_cast<double>(N) / ARRAY_DIM));
                    }

                    P_cols = find_largest_factor_le(NUM_CORES, ideal_p_cols);
                    P_rows = NUM_CORES / P_cols;

                    uint64_t tile_M = (M + P_rows - 1) / P_rows;
                    uint64_t tile_N = (N + P_cols - 1) / P_cols;

                    int bandwidth_per_core = TOTAL_DRAM_BANDWIDTH / NUM_CORES;
                    if (bandwidth_per_core == 0) bandwidth_per_core = 1;

                    all_tasks.insert({ tile_M, tile_N, K, bandwidth_per_core });
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error processing file " << et_path << ": " << e.what() << std::endl;
        }
    }
    std::cout << "Found " << all_tasks.size() << " unique sharded compute tasks in total." << std::endl;

    // ---------------------------------------------------------------
    // 3. Filter Out Tasks Already in Cache
    // ---------------------------------------------------------------
    std::cout << "\n### Step 2: Comparing with existing cache..." << std::endl;
    std::set<ComputeTask> existing_tasks = load_existing_cache(cache_file_path);
    std::vector<ComputeTask> tasks_to_run;

    for (const auto& task : all_tasks) {
        if (existing_tasks.find(task) == existing_tasks.end()) {
            tasks_to_run.push_back(task);
        }
    }

    if (tasks_to_run.empty()) {
        std::cout << "All tasks are already in the cache. Nothing to do." << std::endl;
        // Create an empty script file to signify completion
        std::ofstream(output_script_path).close();
        return 0;
    }

    std::cout << tasks_to_run.size() << " new tasks need to be simulated by Scale-Sim." << std::endl;

    // ---------------------------------------------------------------
    // 4. Generate Scale-Sim Commands
    // ---------------------------------------------------------------
// in main function, after collecting tasks_to_run

    // ...

    std::cout << "\n### Step 3: Generating Scale-Sim runner script '" << output_script_path << "'..." << std::endl;
    // std::filesystem::create_directories(temp_dir);
    std::filesystem::create_directories(work_dir);
    std::ofstream script_file(output_script_path);

    // 1. 在主脚本中定义一个可重复使用的函数
    script_file << "#!/bin/bash" << std::endl;
    script_file << "set -e" << std::endl << std::endl;

    script_file << "export SCALESIM_ROOT=\"" << scalesim_root << "\"" << std::endl;
    // script_file << "export TEMP_DIR=\"" << temp_dir << "\"" << std::endl << std::endl;
    script_file << "export SCALESIM_WORKDIR=\"" << work_dir << "\"" << std::endl;
    script_file << std::endl;

    script_file << "run_and_parse() {" << std::endl;
    script_file << "    local M=$1 N=$2 K=$3 BW=$4 RUN_NAME=$5" << std::endl;
    script_file << "    local CONFIG_PATH=\"${SCALESIM_WORKDIR}/${RUN_NAME}.cfg\"" << std::endl;
    script_file << "    local TOPO_PATH=\"${SCALESIM_WORKDIR}/${RUN_NAME}.csv\"" << std::endl;
    script_file << "    local OUT_DIR=\"${SCALESIM_WORKDIR}/${RUN_NAME}\"" << std::endl;
    script_file << "    local RESULT_REPORT=\"${OUT_DIR}/${RUN_NAME}/COMPUTE_REPORT.csv\"" << std::endl;
    script_file << "    local FINAL_RESULT_FILE=\"${OUT_DIR}/final_result.txt\"" << std::endl;

    script_file << "    echo \"Running task: M=$M, N=$N, K=$K, BW=$BW\"" << std::endl;

    // create_scalesim_config 的逻辑
    script_file << "    cat > \"${CONFIG_PATH}\" << EOF" << std::endl;
    script_file << "[general]" << std::endl;
    script_file << "run_name = ${RUN_NAME}" << std::endl << std::endl;
    script_file << "[architecture_presets]" << std::endl;
    script_file << "ArrayHeight:    " << ARRAY_DIM << std::endl;
    script_file << "ArrayWidth:     " << ARRAY_DIM << std::endl;
    script_file << "IfmapSramSzkB:    6144" << std::endl;
    script_file << "FilterSramSzkB:   6144" << std::endl;
    script_file << "OfmapSramSzkB:    2048" << std::endl;
    script_file << "IfmapOffset:    0" << std::endl;
    script_file << "FilterOffset:   10000000" << std::endl;
    script_file << "OfmapOffset:    20000000" << std::endl;
    script_file << "Dataflow : os" << std::endl;
    script_file << "Bandwidth : ${BW}" << std::endl;
    script_file << "ReadRequestBuffer: 512" << std::endl;
    script_file << "WriteRequestBuffer: 512" << std::endl << std::endl;
    script_file << "[layout]" << std::endl;
    script_file << "IfmapCustomLayout: False" << std::endl;
    script_file << "IfmapSRAMBankBandwidth: 10" << std::endl;
    script_file << "IfmapSRAMBankNum: 10" << std::endl;
    script_file << "IfmapSRAMBankPort: 2" << std::endl;
    script_file << "FilterCustomLayout: False" << std::endl;
    script_file << "FilterSRAMBankBandwidth: 10" << std::endl;
    script_file << "FilterSRAMBankNum: 10" << std::endl;
    script_file << "FilterSRAMBankPort: 2" << std::endl << std::endl;
    script_file << "[sparsity]" << std::endl;
    script_file << "SparsitySupport: false" << std::endl << std::endl;
    script_file << "SparseRep: ellpack_block" << std::endl;
    script_file << "OptimizedMapping: false" << std::endl;
    script_file << "BlockSize: 8" << std::endl;
    script_file << "RandomNumberGeneratorSeed: 40" << std::endl << std::endl;
    script_file << "[run_presets]" << std::endl;
    script_file << "InterfaceBandwidth: USER" << std::endl;
    script_file << "UseRamulatorTrace: False" << std::endl;
    script_file << "EOF" << std::endl;

    // create_scalesim_topology
    script_file << "    echo \"Layer,M,N,K,\" > \"${TOPO_PATH}\"" << std::endl;
    script_file << "    echo \"matmul,${M},${N},${K},\" >> \"${TOPO_PATH}\"" << std::endl;

    // 执行和解析
    script_file << "    (python3 \"${SCALESIM_ROOT}/scalesim/scale.py\" -c \"${CONFIG_PATH}\" -t \"${TOPO_PATH}\" -i gemm -p \"${OUT_DIR}\" -s N && " << std::endl;
    script_file << "    CYCLE=$(HEADER=$(head -n 1 \"${RESULT_REPORT}\") && DATA_LINE=$(head -n 2 \"${RESULT_REPORT}\" | tail -n 1) && COL_NUM=$(echo \"$HEADER\" | tr ',' '\\n' | grep -n \"Total Cycles (incl. prefetch)\" | cut -d: -f1) && echo \"$DATA_LINE\" | cut -d',' -f\"$COL_NUM\") && " << std::endl;
    script_file << "    echo \"${M},${N},${K},${BW},${CYCLE}\" > \"${FINAL_RESULT_FILE}\") || " << std::endl;
    script_file << "    echo \"[ERROR] Task failed: M=${M}, N=${N}, K=${K}\"" << std::endl;
    script_file << "}" << std::endl;

    script_file << "export -f run_and_parse" << std::endl << std::endl;

    // 2. 创建命令列表文件，这次只包含参数
    std::string task_params_file = work_dir + "/task_params.txt";
    std::ofstream params_file(task_params_file);
    pid_t pid = getpid();
    int task_counter = 0;
    for (const auto& task : tasks_to_run) {
        uint64_t M, N, K;
        int BW;
        std::tie(M, N, K, BW) = task;
        std::string run_name = "ss_run_" + std::to_string(pid) + "_" + std::to_string(task_counter++);
        params_file << M << " " << N << " " << K << " " << BW << " " << run_name << std::endl;
    }
    params_file.close();

    // 3. xargs 现在调用我们定义的函数
    script_file << "PARALLEL_JOBS=${SCALE_SIM_JOBS:-$(($(nproc)/2))}" << std::endl;
    script_file << "echo \"Starting " << tasks_to_run.size() << " Scale-Sim tasks with up to $PARALLEL_JOBS parallel jobs...\"" << std::endl << std::endl;
    script_file << "cat " << task_params_file << " | xargs -P $PARALLEL_JOBS -n 5 bash -c 'run_and_parse \"$@\"' _" << std::endl;

    // ... (结果合并和清理)
    script_file << std::endl;
    script_file << "echo 'All Scale-Sim runs complete. Merging results...'" << std::endl;
    script_file << "set +e" << std::endl;
    script_file << "SUCCESSFUL_RESULTS=$(find " << work_dir << " -name 'final_result.txt' | wc -l | tr -d ' ')" << std::endl;
    script_file << "echo \"Found ${SUCCESSFUL_RESULTS:-0} successful simulation results to merge.\"" << std::endl;
    script_file << "find " << work_dir << " -name 'final_result.txt' -exec cat {} + >> " << cache_file_path << std::endl;
    script_file << "set -e" << std::endl;
    script_file << "echo 'Cache file updated. Cleaning up temporary files...'" << std::endl;
    script_file << "rm -rf " << work_dir << "/*" << std::endl;
    script_file << "echo 'Done.'" << std::endl;

    script_file.close();

    std::cout << "\nSuccessfully generated script. To run Scale-Sim, execute:" << std::endl;
    std::cout << "bash " << output_script_path << std::endl;

    return 0;
}