#!/bin/bash

# Set executable paths
SCRIPT_DIR=$(dirname "$(realpath $0)")
ASTRA_SIM=${SCRIPT_DIR}/build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Unaware

# TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
# TRAFFIC_FILENAME="traffic_flow_stats_${TIMESTAMP}"
# echo "Generated traffic stats filename: ${TRAFFIC_FILENAME}"

# Run ASTRA-sim
export ASAN_OPTIONS=detect_leaks=0
${ASTRA_SIM} \
    --workload-configuration=${SCRIPT_DIR}/../symbolic_tensor_graph/results/workload \
    --system-configuration=${SCRIPT_DIR}/inputs/test_cluster.json \
    --network-configuration=${SCRIPT_DIR}/inputs/test_cluster.yml \
    --remote-memory-configuration=${SCRIPT_DIR}/inputs/RemoteMemory.json \
    --comm-group-configuration=${SCRIPT_DIR}/../symbolic_tensor_graph/results/workload.json \
    --enable-traffic-stats=false
    # --traffic-stats-dir=/home/bwoah/my-projects/Multi-Agent_DSE_Framework/arch_simulation/astra-sim/outputs \
    # --traffic-stats-filename=${TRAFFIC_FILENAME}
