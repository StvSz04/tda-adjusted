import re
import sys
import os

def clean_type_token(raw_token):
    # Example raw_token: "%struct.plane_s = type { i32, ... }**"
    if "=" in raw_token:
        name_match = re.search(r'(%struct\.[a-zA-Z0-9_.]+)', raw_token)
        if name_match:
            struct_name = name_match.group(1)
            stars = "*" * raw_token.count('*')
            return f"{struct_name}{stars}"
    return raw_token.strip()

def parse_llvm(file_path):
    with open(file_path, 'r') as f:
        lines = f.readlines()

    token_map = {}
    # Pass 1: Build the Token Map
    for line in lines:
        if 'TYPE_TOKEN:' in line:
            match = re.search(r'!(\d+)\s*=\s*.*TYPE_TOKEN:([^"]+)"', line)
            if match:
                token_id = match.group(1)
                value = clean_type_token(match.group(2).strip())
                token_map[token_id] = value

    output_lines = []
    
    # Pass 2: Process instructions
    for index, line in enumerate(lines):
        # --- FIX 1: THE GUARD ---
        # We must allow lines that have !arg_type or !call_arg_type, not just !restored_type
        if not any(x in line for x in ["!restored_type", "!arg_type", "!call_arg_type", "define"]):
            output_lines.append(line)
            continue

        modified_line = line

        # --- FIX 2: THE CALL/DEFINE BRANCHES ---
        # These use multiple !arg_type tags instead of a single !restored_type
        if "call " in line:            
            # Find all metadata IDs for arguments: !call_arg_type_0 !7, etc.
            arg_ids = re.findall(r'!(?:call_)?arg_type_\d+\s+!(\d+)', line)
            
            for m_id in arg_ids:
                arg_type = token_map.get(m_id, "ptr")
                # Surgical swap: replace the NEXT standalone 'ptr' word
                # count=1 ensures we replace them in the order they appear
                line = re.sub(r'\bptr\b', arg_type, line, count=1)
            
            # If there's also a return type (!restored_type), handle it
            ret_match = re.search(r'!restored_type\s+!(\d+)', line)
            if ret_match and "call ptr" in line:
                ret_type = token_map.get(ret_match.group(1), "ptr")
                line = line.replace("call ptr", f"call {ret_type}", 1)
            
            modified_line = line

        elif line.startswith("define "):
            header_match = re.match(r'(define\s+.*?\s+@\w+\()([^)]*)(\).*)', line)
            
            if header_match:
                prefix, old_args_str, suffix = header_match.groups()
                arg_types = {}

                # 1. NEW: Look for metadata on the SAME line first (in case lines got squashed)
                found_args = re.findall(r'!arg_type_(\d+)\s+!(\d+)', line)
                for arg_idx, type_id in found_args:
                    arg_types[int(arg_idx)] = token_map.get(type_id, "ptr")

                # 2. Look for metadata on the next few lines (The "Peek")
                # Ensure 'index' is your current line number in the list 'all_lines'
                for j in range(1, 6): 
                    if index + j >= len(lines): break
                    peek_line = lines[index + j]
                    found_args = re.findall(r'!arg_type_(\d+)\s+!(\d+)', peek_line)
                    for arg_idx, type_id in found_args:
                        arg_types[int(arg_idx)] = token_map.get(type_id, "ptr")

                # 3. Rebuild the arguments
                old_args_list = old_args_str.split(',')
                new_args_list = []

                for i, arg_blob in enumerate(old_args_list):
                    arg_blob = arg_blob.strip()
                    if "ptr" in arg_blob:
                        # Now arg_types should actually have your data!
                        real_type = arg_types.get(i, "ptr")
                        new_arg = arg_blob.replace("ptr", real_type, 1)
                        new_args_list.append(new_arg)
                    else:
                        new_args_list.append(arg_blob)

                restored_args_str = ", ".join(new_args_list)
                modified_line = f"{prefix}{restored_args_str}{suffix}"
            
        # --- THE RESTORED_TYPE BRANCHES (Alloca, Store, Load, GEP) ---
        else:
            m_id_match = re.search(r'!restored_type\s+!(\d+)', line)
            if not m_id_match:
                output_lines.append(line)
                continue
                
            res_type = token_map.get(m_id_match.group(1), "ptr")
        
            if "alloca " in line:
                # res_type is the type of the register (e.g., %struct.plane_s**)
                # alloca adds one '*', so we strip one to get the allocation type
                base_type = re.sub(r'\*$', '', res_type.strip())
                modified_line = line.replace("alloca ptr", f"alloca {base_type}")

            elif "store " in line:
                # 1. Try the Heuristic: Look for a concrete type already in the 'Value' slot
                concrete_match = re.search(r'store\s+((?!ptr\b)[%@a-zA-Z0-9._[\]]+)\s+[^,]+,\s+ptr\b', line)
                
                if concrete_match:
                    value_type = concrete_match.group(1).strip()
                    destination_type = f"{value_type}*"
                    line = re.sub(r',\s+ptr\b', rf', {destination_type}', line)
                
                else:
                    # 2. Both are 'ptr'. We need to figure out what the value (first ptr) is.
                    # Priority A: Check if there's an explicit !arg_type tag for the value
                    arg_match = re.search(r'!arg_type_\d+\s+!(\d+)', line)
                    if arg_match:
                        val_id = arg_match.group(1)
                        val_type = token_map.get(val_id, "ptr")
                    else:
                        # Priority B: Fallback to stripping the destination metadata
                        val_type = re.sub(r'\*$', '', res_type.strip())

                    # The destination is always the full res_type (!restored_type)
                    dest_type = res_type.strip()
                    
                    # Surgical replacement: 1st ptr = val_type, 2nd ptr = dest_type
                    line = re.sub(r'\bptr\b', val_type, line, count=1)
                    line = re.sub(r'\bptr\b', dest_type, line, count=1)

                modified_line = line

            elif "load " in line:
                        # 1. Try the Heuristic: Look for a concrete type already in the 'Result' slot
                        # e.g., %6 = load i32, ptr %2
                        concrete_match = re.search(r'load\s+((?!ptr\b)[%@a-zA-Z0-9._[\]]+),\s+ptr\b', line)
                        
                        if concrete_match:
                            # We found a concrete result type (e.g., "i32" or "%struct.plane_s*")
                            val_type = concrete_match.group(1).strip()
                            # Mirror it: Source must be one level deeper (*)
                            ptr_src_type = f"{val_type}*"
                            # Replace only the remaining 'ptr' (the source)
                            line = re.sub(r',\s+ptr\b', rf', {ptr_src_type}', line)
                        
                        else:
                            # 2. Both are 'ptr'. Fallback to Metadata (!restored_type)
                            # res_type is your !7 ("%struct.plane_s*")
                            val_type = res_type.strip()
                            # To load a *, you MUST load from a **
                            ptr_src_type = f"{val_type}*"

                            # Surgical replacement: 1st ptr = val, 2nd ptr = src
                            line = re.sub(r'\bptr\b', val_type, line, count=1)
                            line = re.sub(r'\bptr\b', ptr_src_type, line, count=1)
                        
                        modified_line = line

            elif "getelementptr" in line:
                # 1. Capture the 'Pointee Type' (the type right after getelementptr/inbounds)
                type_match = re.search(r'(?:getelementptr\s+(?:inbounds\s+)?)([^,]+)', line)
                
                if type_match:
                    pointee_type = type_match.group(1).strip()
                    
                    # Check if we are dealing with a Global Variable (starts with @)
                    global_match = re.search(r',\s+ptr\s+(@[a-zA-Z0-9._]+)', line)
                    
                    if pointee_type == "ptr":
                        # Scenario: getelementptr ptr, ptr %var
                        clean_ptr = res_type.strip()
                        clean_elem = clean_ptr.rstrip('*')
                        line = re.sub(r'(getelementptr\s+(?:inbounds\s+)?)ptr\b', rf'\1{clean_elem}', line)
                        line = re.sub(r',\s+ptr\b', rf', {clean_ptr}', line)
                    
                    elif global_match:
                        global_name = global_match.group(1)
                        found_global_type = None

                        for search_line in lines:
                            if search_line.strip().startswith(global_name + " ="):
                                # This regex looks for 'constant' or 'global'
                                # Then it captures everything until it sees:
                                # 1. A space followed by '[' (the start of the data values)
                                # 2. The word 'align'
                                # 3. The metadata marker '!'
                                type_search = re.search(r'(?:constant|global)\s+(.+?)(?=\s+\[|\s+align|\s+!)', search_line)
                                if type_search:
                                    found_global_type = type_search.group(1).strip()
                                    break
                        
                        # Fallback to metadata if search fails
                        if not found_global_type or found_global_type == "ptr":
                            found_global_type = res_type.strip().rstrip('*')

                        # Apply the types
                        real_ptr_type = f"{found_global_type}*"
                        
                        # Use replace instead of re.sub for the first part to avoid regex character clashing with [ ]
                        line = line.replace(f"getelementptr inbounds {pointee_type}", f"getelementptr inbounds {found_global_type}")
                        line = line.replace(f"getelementptr {pointee_type}", f"getelementptr {found_global_type}")
                        
                        # Update the pointer operand
                        line = re.sub(rf',\s+ptr\s+{re.escape(global_name)}', f', {real_ptr_type} {global_name}', line)

                        # Dimensionality Fix
                        if "x [" in found_global_type and line.count(',') < 3:
                            line = line.replace(f"{global_name},", f"{global_name}, i64 0,")

                    else:
                        # HEURISTIC: Standard local pointer replacement
                        expected_ptr_type = f"{pointee_type}*"
                        line = re.sub(r',\s+ptr\b', rf', {expected_ptr_type}', line)

                modified_line = line
                
            else:
                # Fallback for simple instructions
                modified_line = line.replace("ptr", res_type)

        # Cleanup: Remove metadata tags for Clang compatibility
        modified_line = re.sub(r',\s*!(?:call_)?arg_type_\d+\s+!\d+', '', modified_line)
        modified_line = re.sub(r',\s*!restored_type\s+!\d+', '', modified_line)
        
        output_lines.append(modified_line)

    # Write output
    output_path = file_path.replace(".ll", "_restored.ll")
    with open(output_path, 'w') as f:
        f.writelines(output_lines)
    
    print(f"DEBUG: Saved to {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 parser.py <file.ll>")
    else:
        parse_llvm(sys.argv[1])