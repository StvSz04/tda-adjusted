import subprocess
import sys
from pathlib import Path
import json


#cd build && cmake --build . && cd .. && python pass_script.py



# def file_to_string_pathlib(file_path):
#     return Path(file_path).read_text(encoding='utf-8')

if __name__ == "__main__":

    results = {}



    plugin = "/mnt/c/Users/iamst/Desktop/TypeDeductionAnalysis-main/build/TypeDeductionAnalysisPlugin.so"
    llvm_dir = "/mnt/c/Users/iamst/Desktop/TypeDeductionAnalysis-main/llvm_files"

    fileNames = [
        "wc_18_O0.ll", "wc_18_O3.ll", 
        "sleep_18_O0.ll", "sleep_18_O3.ll", 
        "sum_18_O0.ll", "sum_18_O3.ll", 
    ]

    # fileNames = ["acas_18_O0.ll"]
    fileNames = ["simple_18_O0.ll"]

    cmds = []

    for f in fileNames:
        # Strip the .ll extension for the binary name (e.g., "wc_18_O0")
        base_name = f.replace(".ll", "")
        
        pipeline = [
            # Step 1: LLVM Pass (Input -> out_file)
            ["opt", f"--load-pass-plugin={plugin}", "-passes=tda,restore-types", "-S", 
            f"{llvm_dir}/{f}", "-o", f"{llvm_dir}/out_{f}"],
            
            # Step 2: Python Parser (out_file -> restored_out_file)
            ["python3", "parser.py", f"{llvm_dir}/out_{f}", f"out_{f}"],
            
            # Step 3: Clang-14 (restored_out_file -> bin_file)
            ["clang-14", f"{llvm_dir}/out_{base_name}_restored.ll", "-o", f"{llvm_dir}/bin_{base_name}"],
        ]
        cmds.append(pipeline)
    # Run all commands
    for i, cmd in enumerate(cmds):
        print("*********************************************")
        print(f"Running cmd{i+1}")
        print("*********************************************")

        for j,sub_cmd in enumerate(cmd):
            print(sub_cmd)
            result = subprocess.run(sub_cmd, capture_output=True, text=True)
            print(result.stderr)
            print(result.stdout)
            
            print("--------------------------------------------")
