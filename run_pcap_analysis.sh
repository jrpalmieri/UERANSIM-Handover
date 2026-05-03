#!/bin/bash

for i in {1..5}
do
    python ./tools/pcap_analysis/rrc_state_machine.py ./tools/dashboard/logs/$1/ran_trace_$i.pcap
done