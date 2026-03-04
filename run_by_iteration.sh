#!/bin/bash

set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
ASTRA_SIM="${SCRIPT_DIR}/build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Unaware"
PROJECT_ROOT=$(realpath "${SCRIPT_DIR}/../..")

ET_FILE_PATH="."
COMM_GROUP_FILE_PATH="."
NETWORK_CONFIG="iteration_data/config/generated_network.yml"
SYSTEM_CONFIG="iteration_data/config/my_cluster.json"
RESULT_DIR="iteration_data/result"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --et-file-path)
            ET_FILE_PATH="$2"
            shift 2
            ;;
        --comm-group-file-path)
            COMM_GROUP_FILE_PATH="$2"
            shift 2
            ;;
        --network-config)
            NETWORK_CONFIG="$2"
            shift 2
            ;;
        --system-config)
            SYSTEM_CONFIG="$2"
            shift 2
            ;;
        --result-dir)
            RESULT_DIR="$2"
            shift 2
            ;;
        *)
            echo "[ASTRA-Sim] Unknown argument: $1"
            exit 1
            ;;
    esac
done

# ---- Required runtime environment for SCALE-Sim integration in Workload.cc ----
# Allow callers to override these paths, but always export concrete values.
export ASTRA_PYTHON_BIN="${ASTRA_PYTHON_BIN:-python3}"
export ASTRA_SCALESIM_INPUT_DIR="${ASTRA_SCALESIM_INPUT_DIR:-${PROJECT_ROOT}/arch_simulation/runtime_luts/scalesim/inputs}"
export ASTRA_SCALESIM_OUTPUT_DIR="${ASTRA_SCALESIM_OUTPUT_DIR:-${PROJECT_ROOT}/arch_simulation/runtime_luts/scalesim/outputs}"
export ASTRA_SCALESIM_LAYOUT_PATH="${ASTRA_SCALESIM_LAYOUT_PATH:-${ASTRA_SCALESIM_INPUT_DIR}/scalesim_layout.csv}"
export ASTRA_SCALESIM_LUT_PATH="${ASTRA_SCALESIM_LUT_PATH:-${ASTRA_SCALESIM_INPUT_DIR}/scalesim_lut.csv}"

if [ -z "${SCALESIM_ROOT:-}" ]; then
    echo "ERROR: SCALESIM_ROOT is not set. Please export SCALESIM_ROOT to your SCALE-Sim repository root."
    exit 1
fi

mkdir -p "${ASTRA_SCALESIM_INPUT_DIR}"
mkdir -p "${ASTRA_SCALESIM_OUTPUT_DIR}"

if [ ! -f "${ASTRA_SCALESIM_LAYOUT_PATH}" ]; then
    echo "ERROR: Missing SCALE-Sim layout file: ${ASTRA_SCALESIM_LAYOUT_PATH}"
    exit 1
fi

timestamp=$(date +"%Y%m%d_%H%M%S")
traffic_filename="traffic_flow_stats_${timestamp}"

resolve_path() {
    local path="$1"
    if [[ "$path" = /* ]]; then
        echo "$path"
    else
        echo "${SCRIPT_DIR}/../$path"
    fi
}

et_file_path_abs=$(resolve_path "$ET_FILE_PATH")
# Extract the base filename without the ".0.et" extension
et_file_base=$(basename "$et_file_path_abs" .0.et)
et_file_dir=$(dirname "$et_file_path_abs")
et_file_path_for_astra="${et_file_dir}/${et_file_base}"

comm_group_file_path_abs=$(resolve_path "$COMM_GROUP_FILE_PATH")
network_config_abs=$(resolve_path "$NETWORK_CONFIG")
system_config_abs=$(resolve_path "$SYSTEM_CONFIG")
result_dir_abs=$(resolve_path "$RESULT_DIR")

mkdir -p "$(dirname "$network_config_abs")"
mkdir -p "$(dirname "$system_config_abs")"
mkdir -p "$result_dir_abs"

json_output_file="${result_dir_abs}/simulation_results.json"

# Run ASTRA-sim and capture exit status
export ASAN_OPTIONS=detect_leaks=0
${ASTRA_SIM} \
    --workload-configuration=${et_file_path_for_astra} \
    --system-configuration=${system_config_abs} \
    --network-configuration=${network_config_abs} \
    --remote-memory-configuration=${SCRIPT_DIR}/inputs/RemoteMemory.json \
    --comm-group-configuration=${comm_group_file_path_abs} \
    --enable-traffic-stats=false \
    --enable-json-summary=true \
    --json-summary-file=${json_output_file} \
    --logging-configuration=empty

# Check exist status
ASTRA_EXIT_CODE=$?
echo "Checking simulation results..."

if [ $ASTRA_EXIT_CODE -ne 0 ]; then
    echo "ERROR: ASTRA-Sim failed with exit code: $ASTRA_EXIT_CODE"
    exit $ASTRA_EXIT_CODE
fi

if [ ! -f "${json_output_file}" ]; then
    echo "ERROR: JSON output file not generated at ${json_output_file}"
    exit 1
fi

if [ ! -s "${json_output_file}" ]; then
    echo "ERROR: JSON output file is empty"
    exit 1
fi

if ! python -c "import json; json.load(open('${json_output_file}'))" 2>/dev/null; then
    echo "ERROR: Invalid JSON format in results file"
    exit 1
fi

echo "Simulation completed successfully."
echo "JSON results available at: ${json_output_file}"
