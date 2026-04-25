import sys
import argparse
import os

def main():
    parser = argparse.ArgumentParser(description="Advanced Unity dump.cs search tool")
    parser.add_argument("path", help="Path to dump.cs")
    parser.add_argument("--method", help="Filter by method name")
    parser.add_argument("--field", help="Filter by field name")
    parser.add_argument("--class", dest="classname", help="Filter by class name")
    parser.add_argument("--ns", dest="namespace", help="Filter by namespace")
    parser.add_argument("--ret", help="Filter by return type")
    parser.add_argument("--params", help="Filter by parameter types (comma separated)")
    parser.add_argument("--definition", help="Show full definition of a class (fields + methods)")
    
    if len(sys.argv) == 1:
        parser.print_help()
        return

    args = parser.parse_args()
    
    if not os.path.exists(args.path):
        print(f"Error: {args.path} not found.")
        return

    current_ns = "-"
    current_class = ""
    in_class = False
    class_buffer = []
    found_any = False

    print(f"Scanning {args.path}...\n")

    with open(args.path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            tline = line.strip()
            
            # Track Namespace
            if tline.startswith("// Namespace: "):
                current_ns = tline[14:] if tline[14:] else "-"
                continue
            
            # Track Class Start
            if " class " in tline or " struct " in tline or " enum " in tline:
                parts = tline.split()
                keyword = next((w for w in ["class", "struct", "enum"] if w in parts), None)
                if keyword:
                    try:
                        idx = parts.index(keyword)
                        current_class = parts[idx+1]
                        in_class = True
                        class_buffer = [line]
                    except: pass
                continue

            if in_class:
                if tline == "}":
                    # Class End - check if we wanted this definition
                    if args.definition and args.definition.lower() in current_class.lower():
                        print(f"// --- Definition for {current_ns}.{current_class} ---")
                        print("".join(class_buffer))
                        print("}\n" + "="*80)
                        found_any = True
                    in_class = False
                    class_buffer = []
                    current_class = ""
                    continue
                else:
                    class_buffer.append(line)
                    
                # Field Filtering (if inside a class and not an RVA line)
                if args.field and ";" in tline and not tline.startswith("//"):
                    # Typical field: private float speed; // 0x10
                    field_part = tline.split(";")[0]
                    field_name = field_part.split()[-1]
                    
                    if args.field.lower() in field_name.lower():
                        match = True
                        if args.classname and args.classname.lower() not in current_class.lower(): match = False
                        if args.namespace and args.namespace.lower() not in current_ns.lower(): match = False
                        
                        if match and not args.definition:
                            print(f"[FIELD] {current_ns}.{current_class} -> {tline}")
                            found_any = True

            # Method Filtering
            if tline.startswith("// RVA:"):
                rva_info = tline
                try:
                    method_line = next(f)
                    mtline = method_line.strip()
                    
                    if "(" in mtline:
                        pre_paren = mtline.split("(")[0].strip()
                        m_parts = pre_paren.split()
                        
                        if len(m_parts) >= 2:
                            method_name = m_parts[-1]
                            ret_type = m_parts[-2]
                            params_str = method_line.split("(")[1].split(")")[0]
                            
                            match = True
                            if args.method and args.method.lower() not in method_name.lower(): match = False
                            if args.classname and args.classname.lower() not in current_class.lower(): match = False
                            if args.namespace and args.namespace.lower() not in current_ns.lower(): match = False
                            if args.ret and args.ret.lower() not in ret_type.lower(): match = False
                            
                            if args.params:
                                for p in args.params.split(","):
                                    if p.strip().lower() not in params_str.lower():
                                        match = False
                            
                            if match and not args.definition and not args.field:
                                print(f"[{current_ns}] {current_class} -> {method_name}")
                                print(f"  Return: {ret_type}")
                                print(f"  Params: ({params_str})")
                                print(f"  {rva_info}\n")
                                found_any = True
                except StopIteration: break

    if not found_any:
        print("No matches found for the given filters.")

if __name__ == "__main__":
    main()
