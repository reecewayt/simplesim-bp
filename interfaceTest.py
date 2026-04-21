import unittest
import subprocess
import re
import os
import numpy as np
from tabulate import tabulate

class bpredTest(unittest.TestCase):
    
    def run_sim(self, sim_type, bpred_type, tage_flag, test_prog):
        if(os.path.basename(os.getcwd()) != "simplesim-bp"):
            raise Exception("Please run the tests from the root directory of the project (simplesim-bp)")
        
        subprocess.run(["make", "clean"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["make", f"TAGE_FLAG={tage_flag}"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cmd = [sim_type,"-bpred", bpred_type, test_prog]
        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.stderr + result.stdout
    
    def run_sim_ref(self, sim_type, bpred_type, test_prog):
        cmd = [sim_type, "-bpred", bpred_type, test_prog]
        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.stderr + result.stdout
    
    def run_sim_bimod(self, sim_type, test_prog):
        cmd = [sim_type, "-bpred:bimod", "4096", test_prog]
        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.stderr + result.stdout

    def parse_stats(self, output):
        pattern = r"(sim_\w+)\s+([\d.]+)"
        sim_stats = dict(re.findall(pattern, output))
        pattern = r"(bpred_[\w\.]+)\s+([\d.]+)"
        bpred_stats = dict(re.findall(pattern, output))
        pattern = r"DIR_MISS: PC=(0x[\da-f]+) actual=(\d+) predicted=(\d+)"
        dir_miss_stats = re.findall(pattern, output)
        return sim_stats, bpred_stats, dir_miss_stats
    
    def compare_cpu_stats(self, dut_stats, ref_stats):
        for key in dut_stats:
            if key.startswith("sim_") and key != "sim_elapsed_time":
                self.assertEqual(dut_stats[key], ref_stats[key], f"sim mismatch: DUT:{key}={dut_stats[key]}, REF:{key}={ref_stats[key]}")
        return True
    
    def compare_bpred_stats_all(self, dut_stats, ref_stats, dut_dir_miss_stats=None, 
                                ref_dir_miss_stats=None,
                                bpred_type=None, ref_type="taken", 
                                sim_type="sim-bpred", 
                                test_prog="tests-pisa/bin.little/test-math",
                                tage_flag="-DTAGE_TAKEN"):
        config = [["Sim Program",f"{sim_type}"], 
                  ["DUT BPred Type", f"{bpred_type}"],
                  ["REF BPred Type", f"{ref_type}"],
                  ["Current TAGE Flag", f"{tage_flag}"],
                  ["Test Program", f"{test_prog}"]]
        crit_stats = ["bpred_{}.bpred_addr_rate".format(bpred_type),
                      "bpred_{}.bpred_dir_rate".format(bpred_type),
                      "bpred_{}.misses".format(bpred_type)]
        mismatches = []
        percent_diff = []
        for key in dut_stats:
                dut_key = f"bpred_{bpred_type}.{'.'.join(key.split('.')[1:])}"
                ref_key = f"bpred_{ref_type}.{'.'.join(key.split('.')[1:])}"
                if ref_key not in ref_stats:
                    continue
                if(dut_stats[dut_key] != ref_stats[ref_key]):
                    mismatches.append([dut_key, dut_stats[dut_key], ref_stats[ref_key]])
        for value in crit_stats:
            dut_key = f"bpred_{bpred_type}.{value.split('.')[1]}"
            ref_key = f"bpred_{ref_type}.{value.split('.')[1]}"
            if dut_stats[dut_key] != ref_stats[ref_key]:
                percent_diff.append([dut_key,np.absolute(((float(dut_stats[dut_key]) - float(ref_stats[ref_key])) 
                                       / float(ref_stats[ref_key]) * 100)) 
                                      if ref_stats[ref_key] != 0 else float('inf')])
        
        if mismatches:
            mismatch_table = tabulate(mismatches, 
                        headers=["Stat Mismatch", f"DUT ({bpred_type})", f"Reference ({ref_type})"],
                        tablefmt="grid")    
            config_table = tabulate(config,
                        headers=["PARAMETER", "VALUE"],
                        tablefmt="grid")
            crit_table = tabulate(percent_diff,
                        headers=["Critical Stat", "Percent Difference"],
                        tablefmt="grid")
            
            # Build output with DIR_MISS counts
            output = ("\n *-----------------Configuration------------------*\n" + config_table +
                      "\n *-----------------Mismatches---------------------*\n" + mismatch_table +
                      "\n *-----------------Critical Stats-----------------*\n" + crit_table)
            
            dut_count = len(dut_dir_miss_stats) if dut_dir_miss_stats else 0
            ref_count = len(ref_dir_miss_stats) if ref_dir_miss_stats else 0
            if dut_count > 0 or ref_count > 0:
                output += f"\n *-----------Unconditional Branch Misses----------*\nDUT: {dut_count} | Reference: {ref_count}"
            
            self.fail(output)
        return True
    
    def compare_bpred_stats_expected(self, dut_stats, ref_stats, bpred_type,ref_type="taken"):
        keys_to_compare = ["lookups","updates","jr_seen", "misses"]    
        for key in keys_to_compare:
            dut_key = f"bpred_{bpred_type}.{key}"
            ref_key = f"bpred_{ref_type}.{key}"
            self.assertEqual(dut_stats[dut_key], ref_stats[ref_key], f"bpred mismatch: {dut_key}={dut_stats[dut_key]}, {ref_key}={ref_stats[ref_key]}")
        return True
    

    def test_tage_bimod(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-bpred", "tage", "-DTAGE_BIMOD", test_prog)
        ref_output = self.run_sim_bimod("./bin/sim-bpred", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats, 
                                     dut_dir_miss_stats, ref_dir_miss_stats,
                                      "tage", "bimod", "sim-bpred", 
                                      test_prog, "-DTAGE_BIMOD")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)

    def test_tage_taken(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-bpred", "tage", "-DTAGE_TAKEN", test_prog)
        ref_output = self.run_sim_ref("./bin/sim-bpred", "taken", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats, 
                                     dut_dir_miss_stats, ref_dir_miss_stats,
                                      "tage", "taken", "sim-bpred", 
                                      test_prog, "-DTAGE_TAKEN")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)
    
    def test_tage_nottaken(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-bpred", "tage", "-DTAGE_NOT_TAKEN", test_prog)
        ref_output = self.run_sim_ref("./bin/sim-bpred", "nottaken", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats, 
                                     dut_dir_miss_stats, ref_dir_miss_stats,
                                      "tage", "nottaken", "sim-bpred", 
                                      test_prog, "-DTAGE_NOT_TAKEN")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)

    def test_tage_bimod_outorder(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-outorder", "tage", "-DTAGE_BIMOD", test_prog)
        ref_output = self.run_sim_bimod("./bin/sim-outorder", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats, 
                                     dut_dir_miss_stats,ref_dir_miss_stats,
                                     "tage", "bimod", "sim-outorder", 
                                     test_prog, "-DTAGE_BIMOD")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)
    
    def test_tage_taken_outorder(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-outorder", "tage", "-DTAGE_TAKEN", test_prog)
        ref_output = self.run_sim_ref("./bin/sim-outorder", "taken", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats,
                                     dut_dir_miss_stats, ref_dir_miss_stats,
                                      "tage", "taken", "sim-outorder", 
                                      test_prog, "-DTAGE_TAKEN")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)

    def test_tage_nottaken_outorder(self,test_prog="tests-pisa/bin.little/test-math"):
        dut_output = self.run_sim("./bin/sim-outorder", "tage", "-DTAGE_NOT_TAKEN", test_prog)
        ref_output = self.run_sim_ref("./bin/sim-outorder", "nottaken", test_prog)
        dut_sim_stats, dut_bpred_stats, dut_dir_miss_stats = self.parse_stats(dut_output)
        ref_sim_stats, ref_bpred_stats, ref_dir_miss_stats = self.parse_stats(ref_output)
        self.compare_bpred_stats_all(dut_bpred_stats, ref_bpred_stats, 
                                     dut_dir_miss_stats, ref_dir_miss_stats,
                                      "tage", "nottaken", "sim-outorder", 
                                      test_prog, "-DTAGE_NOT_TAKEN")
        self.compare_cpu_stats(dut_sim_stats, ref_sim_stats)


if __name__ == '__main__':
    
    unittest.main(verbosity=2)
    
            
        
    
