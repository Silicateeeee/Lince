/**
 * Advanced Unity Snoop Tool for LAUGH
 * Interactive field inspection and method exploring.
 */

var searchName = "Player";
var dumpPath = "/home/neverflag/Downloads/GameAssembly_il2cppdump_20260424_0337/dump.cs";
var foundObjects = [];
var selectedObject = null;
var selectedClass = null;
var classFields = [];
var isSearching = false;

async function performSnoop() {
    if (isSearching) return;
    isSearching = true;
    foundObjects = [];
    
    log("Searching for GameObjects named: " + searchName);
    var results = await memory.AOB(stringToHex(searchName));
    
    if (results.length > 0) {
        log("Found " + results.length + " potential name matches.");
        foundObjects = results.map(addr => ({
            name: searchName,
            address: addr
        }));
    }
    isSearching = false;
}

function stringToHex(str) {
    var hex = "";
    for (var i = 0; i < str.length; i++) {
        hex += str.charCodeAt(i).toString(16).padStart(2, '0') + " ";
    }
    return hex.trim();
}

async function inspectClass(fullClassName) {
    selectedClass = fullClassName;
    var parts = fullClassName.split(".");
    var className = parts.pop();
    var ns = parts.join(".");
    
    log("Fetching fields for: " + fullClassName);
    classFields = unity.getFields(ns, className);
    log("Found " + classFields.length + " fields.");
}

function onUpdate() {}

function onGUI() {
    if (gui.beginWindow("Unity Snoop & Interact")) {
        
        if (unity.isLoading()) {
            gui.text("Loading Dump... " + (unity.getLoadProgress() * 100).toFixed(1) + "%");
            gui.progressBar(unity.getLoadProgress());
        } else if (!unity.isLoaded()) {
            dumpPath = gui.inputText("Dump Path", dumpPath);
            if (gui.button("Load Unity Dump")) unity.load(dumpPath);
        } else {
            // Main Snoop UI
            searchName = gui.inputText("GameObject Name", searchName);
            if (isSearching) {
                gui.progressBar(memory.getProgress());
            } else {
                if (gui.button("Search in Memory")) performSnoop();
            }

            gui.separator();
            
            // Layout: 2 Columns
            gui.beginChild("ObjectList", 300, 300);
            gui.text("Objects in Memory:");
            foundObjects.forEach((obj) => {
                if (gui.button(obj.name + " @ 0x" + obj.address.toString(16).toUpperCase())) {
                    selectedObject = obj;
                }
            });
            gui.endChild();
            
            gui.sameLine();
            
            gui.beginChild("Inspector", 0, 300);
            if (selectedObject) {
                gui.text("Inspecting: " + selectedObject.name);
                if (gui.button("Find Matching Classes")) {
                    var classes = unity.searchClasses(selectedObject.name);
                    selectedObject.potentialClasses = classes;
                }
                
                if (selectedObject.potentialClasses) {
                    gui.text("Select Class Definition:");
                    selectedObject.potentialClasses.forEach(c => {
                        if (gui.button(c)) inspectClass(c);
                    });
                }
            }
            gui.endChild();

            if (selectedClass && selectedObject) {
                gui.separator();
                gui.text("Fields for " + selectedClass);
                gui.beginChild("FieldsList", 0, 0);
                
                classFields.forEach(f => {
                    var fieldAddr = selectedObject.address + f.offset;
                    var val = "???";
                    
                    // Attempt to read based on type string
                    if (f.type == "float") val = memory.read(fieldAddr, 4).toFixed(3);
                    else if (f.type == "int") val = memory.read(fieldAddr, 2);
                    else if (f.type == "bool") val = memory.read(fieldAddr, 0) != 0;
                    else val = "(complex/unsupported)";

                    gui.text(f.name + " (" + f.type + "): " + val);
                    gui.sameLine(300);
                    if (gui.button("Set##" + f.name)) {
                        // Example: toggle bools or zero out numbers
                        if (f.type == "float") memory.write(fieldAddr, 0.0, 4);
                        if (f.type == "bool") memory.write(fieldAddr, 1, 0);
                    }
                });
                
                gui.endChild();
            }
        }
        
        gui.endWindow();
    }
}
