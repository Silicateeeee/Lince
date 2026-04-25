import sys
import os

def find_method(dump_path, target_method):
    if not os.path.exists(dump_path):
        print(f"Error: File not found: {dump_path}")
        return

    current_ns = "Unknown"
    current_class = "Unknown"
    
    found_count = 0
    print(f"Searching for \"{target_method}\" in {dump_path}...\n")
    
    with open(dump_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("// Namespace: "):
                current_ns = stripped[14:] if stripped[14:] else "-"
            elif " class " in stripped:
                # Basic class name extraction
                parts = stripped.split()
                if "class" in parts:
                    idx = parts.index("class")
                    if idx + 1 < len(parts):
                        current_class = parts[idx + 1]
            elif stripped.startswith("// RVA: "):
                # The actual method signature is usually on the next line
                try:
                    method_line = next(f).strip()
                    if target_method.lower() in method_line.lower() and "(" in method_line:
                        print(f"[{found_count + 1}]")
                        print(f"  Namespace: {current_ns}")
                        print(f"  Class:     {current_class}")
                        print(f"  Signature: {method_line}")
                        print(f"  RVA Info:  {stripped}\n")
                        found_count += 1
                except StopIteration:
                    break
                    
    if found_count == 0:
        print(f"No method matching \"{target_method}\" was found.")
    else:
        print(f"Search complete. Found {found_count} matches.")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 find_method.py <path_to_dump.cs> <method_name>")
        print("Example: python3 find_method.py ./dump.cs Update")
    else:
        find_method(sys.argv[1], sys.argv[2])
