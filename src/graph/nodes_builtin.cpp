// The nodes every game has: events, flow, the rex API (hooks, registers, memory, input, cvars, switches,
// logging) and plain logic and math. Each one's Lua is what a mod would write by hand; lua_mods.md is the API
// they stand for.

#include <algorithm>
#include <string>
#include <vector>

#include "lua_compiler.h"
#include "node_library.h"

namespace rexfabric::graph {
namespace {

// The key names the game's input understands.
const std::vector<std::string>& KeyNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> keys;
    for (int i = 1; i <= 12; ++i) keys.push_back("F" + std::to_string(i));
    for (char c = 'A'; c <= 'Z'; ++c) keys.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) keys.push_back(std::string(1, c));
    for (const char* key : {"Space", "Return", "Escape", "Tab", "Backspace", "Shift", "Control", "Alt", "Up", "Down", "Left", "Right", "Insert", "Delete", "Home", "End", "PageUp", "PageDown", "Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket", "RBracket", "Quote"}) keys.push_back(key);
    for (int i = 0; i <= 9; ++i) keys.push_back("Numpad" + std::to_string(i));
    for (const char* key : {"NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash"}) keys.push_back(key);
    return keys;
  }();
  return names;
}

const std::vector<std::string>& RegisterNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> registers;
    for (int i = 0; i < 32; ++i) registers.push_back("r" + std::to_string(i));
    for (int i = 0; i < 32; ++i) registers.push_back("f" + std::to_string(i));
    registers.push_back("lr");
    registers.push_back("ctr");
    return registers;
  }();
  return names;
}

std::vector<std::string> Counts(int first, int last) {
  std::vector<std::string> counts;
  for (int i = first; i <= last; ++i) counts.push_back(std::to_string(i));
  return counts;
}

int CountOf(const Node& node, std::string_view setting) {
  const std::string text = SettingOf(node, setting);
  return text.empty() ? 0 : std::stoi(text);
}

// What a register holds: f registers floats, the rest integers.
PinKind RegisterKind(std::string_view name) { return !name.empty() && name[0] == 'f' ? PinKind::kFloat : PinKind::kInt; }

// What rex.mem's reader of this type gives back.
PinKind MemoryKind(std::string_view type) {
  if (type == "f32" || type == "f64") return PinKind::kFloat;
  if (type == "string" || type == "wstring") return PinKind::kString;
  return PinKind::kInt;
}

PinKind CvarKind(std::string_view type) {
  if (type == "boolean") return PinKind::kBool;
  if (type == "number") return PinKind::kFloat;
  if (type == "string") return PinKind::kString;
  return PinKind::kInt;
}

// Swaps the pin named `name` for one of `kind`, keeping its place.
void Retype(std::vector<Pin>& pins, std::string_view name, PinKind kind) {
  for (Pin& pin : pins) {
    if (pin.name == name) pin = DataPin(name, kind);
  }
}

std::string Joined(const std::vector<std::string>& parts) {
  std::string joined;
  for (size_t i = 0; i < parts.size(); ++i) joined += (i ? ", " : "") + parts[i];
  return joined;
}

std::string Quoted(Emitter& emit, std::string_view setting) { return LuaQuote(emit.Setting(setting)); }

std::string Titled(const Node& node, std::string_view prefix, std::string_view setting, std::string_view suffix = {}) {
  const std::string value = SettingOf(node, setting);
  return std::string(prefix) + (value.empty() ? "?" : value) + std::string(suffix);
}

// A text setting naming a cvar or switch: empty is an error.
std::string NamedSetting(Emitter& emit, std::string_view setting) {
  if (emit.Setting(setting).empty()) emit.Error("Needs a name");
  return Quoted(emit, setting);
}

// Pure nodes ----------------------------------------------------------------------------------------------

NodeDesc Pure(std::string id, std::string title, std::string category, std::string description, std::vector<Pin> inputs, Pin output, std::function<std::string(Emitter&)> expression) {
  NodeDesc desc{.id = std::move(id), .title = std::move(title), .category = std::move(category), .description = std::move(description), .role = NodeRole::kPure};
  desc.pins = {std::move(inputs), {std::move(output)}};
  desc.expression = [expression = std::move(expression)](Emitter& emit, std::string_view) { return expression(emit); };
  return desc;
}

// A and B into Result, as `(A op B)`.
NodeDesc Binary(std::string id, std::string title, std::string category, std::string description, PinKind in, PinKind out, std::string op) {
  return Pure(std::move(id), std::move(title), std::move(category), std::move(description), {DataPin("A", in), DataPin("B", in)}, DataPin("Result", out), [op = std::move(op)](Emitter& emit) { return "(" + emit.Input("A") + " " + op + " " + emit.Input("B") + ")"; });
}

// Value into Result, as `function(Value)`.
NodeDesc Unary(std::string id, std::string title, std::string category, std::string description, PinKind in, PinKind out, std::string function) {
  return Pure(std::move(id), std::move(title), std::move(category), std::move(description), {DataPin("Value", in)}, DataPin("Result", out), [function = std::move(function)](Emitter& emit) { return function + "(" + emit.Input("Value") + ")"; });
}

// Events -----------------------------------------------------------------------------------------------

// rex.on(name, function(params) chain end)
void EmitListener(Emitter& emit, const std::string& event, const std::vector<std::string>& params) {
  emit.Line("rex.on(" + event + ", function(" + Joined(params) + ")");
  emit.Open();
  emit.Chain(kExecOut);
  emit.Close();
  emit.Line("end)");
}

void AddEvents(Library& library) {
  const std::string category = "Events";

  library.Add({.id = "event.start", .title = "On Start", .category = category,
      .description = "Runs once as the scripts load, before the game's first frame. Set things up here: settings, variables. The game can't be called yet.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        emit.Line("do");
        emit.Open();
        emit.Chain(kExecOut);
        emit.Close();
        emit.Line("end");
      }});

  library.Add({.id = "event.frame", .title = "On Frame", .category = category,
      .description = "Runs every frame, with the seconds since the last one.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut), DataPin("Seconds", PinKind::kFloat)}},
      .emit = [](Emitter& emit) { EmitListener(emit, "\"frame\"", {emit.Param("Seconds")}); }});

  library.Add({.id = "event.hook_before", .title = "Before Function", .category = category,
      .description = "Runs each time the game calls the function at Address, before it runs. Registers are its arguments: r3 is the first. Skip Original stops the function itself from running.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut), DataPin("Registers", PinKind::kRegisters)}},
      .settings = {{.name = "Address", .kind = SettingKind::kAddress, .tooltip = "The guest function, like 0x82558D28"}},
      .title_of = [](const Node& node) { return Titled(node, "Before ", "Address"); },
      .emit = [](Emitter& emit) {
        emit.CheckHookable("Address");
        emit.Line("rex.hook(" + emit.Address("Address") + ", function(" + emit.Param("Registers") + ")");
        emit.Open();
        emit.Chain(kExecOut);
        if (emit.HasEventLocal("skip")) emit.Line("if " + emit.EventLocal("skip") + " then return false end");
        emit.Close();
        emit.Line("end)");
      }});

  library.Add({.id = "event.hook_after", .title = "After Function", .category = category,
      .description = "Runs each time the function at Address returns. r3 in Registers is what it returned, and can be changed.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut), DataPin("Registers", PinKind::kRegisters)}},
      .settings = {{.name = "Address", .kind = SettingKind::kAddress, .tooltip = "The guest function, like 0x8232C048"}},
      .title_of = [](const Node& node) { return Titled(node, "After ", "Address"); },
      .emit = [](Emitter& emit) {
        emit.CheckHookable("Address");
        emit.Line("rex.hook_after(" + emit.Address("Address") + ", function(" + emit.Param("Registers") + ")");
        emit.Open();
        emit.Chain(kExecOut);
        emit.Close();
        emit.Line("end)");
      }});

  library.Add({.id = "event.midasm", .title = "Midasm Site", .category = category,
      .description = "Claims a midasm site the game compiled in. Registers are the ones the site lends, by name. At a branch site, Take Branch takes the branch.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut), DataPin("Registers", PinKind::kRegisters)}},
      .settings = {{.name = "Address", .kind = SettingKind::kAddress, .tooltip = "The site's address from the game's midasm config"}},
      .title_of = [](const Node& node) { return Titled(node, "Midasm ", "Address"); },
      .emit = [](Emitter& emit) {
        emit.Line("rex.midasm(" + emit.Address("Address") + ", function(" + emit.Param("Registers") + ")");
        emit.Open();
        emit.Chain(kExecOut);
        if (emit.HasEventLocal("take")) emit.Line("return " + emit.EventLocal("take"));
        emit.Close();
        emit.Line("end)");
      }});

  library.Add({.id = "event.key", .title = "Key Pressed", .category = category,
      .description = "Runs once when the key goes down, while the game window has focus and no menu wants the keyboard.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut)}},
      .settings = {{.name = "Key", .kind = SettingKind::kChoice, .initial = "F9", .choices = KeyNames()}},
      .title_of = [](const Node& node) { return Titled(node, "Key ", "Key", " Pressed"); },
      .emit = [](Emitter& emit) {
        // rex.input.pressed tracks one edge per key, so a second node on the same key would never see a press.
        for (const Node& other : emit.graph().nodes()) {
          if (other.id != emit.node().id && other.type == emit.node().type && SettingOf(other, "Key") == emit.Setting("Key")) emit.Warn("Another Key Pressed node waits for " + emit.Setting("Key") + "; only one of them sees each press");
        }
        emit.Line("rex.on(\"frame\", function()");
        emit.Open();
        emit.Line("if rex.input.pressed(" + Quoted(emit, "Key") + ") and rex.input.accepts() then");
        emit.Indent();
        emit.Chain(kExecOut);
        emit.Dedent();
        emit.Line("end");
        emit.Close();
        emit.Line("end)");
      }});

  library.Add({.id = "event.setting_changed", .title = "Setting Changed", .category = category,
      .description = "Runs on the frame after the cvar named Setting changes, with its new value.",
      .role = NodeRole::kEvent, .pins = {{}, {ExecPin(kExecOut), DataPin("Value", PinKind::kAny)}},
      .settings = {{.name = "Setting", .kind = SettingKind::kText, .tooltip = "The cvar's name"}},
      .title_of = [](const Node& node) { return Titled(node, "", "Setting", " Changed"); },
      .emit = [](Emitter& emit) {
        emit.Line("rex.cvar.on_change(" + NamedSetting(emit, "Setting") + ", function(_, " + emit.Param("Value") + ")");
        emit.Open();
        emit.Chain(kExecOut);
        emit.Close();
        emit.Line("end)");
      }});
}

// Flow -------------------------------------------------------------------------------------------------

void AddFlow(Library& library) {
  const std::string category = "Flow";

  library.Add({.id = "flow.branch", .title = "Branch", .category = category,
      .description = "Goes on through True or False.",
      .pins = {{ExecPin(), DataPin("Condition", PinKind::kBool, true)}, {ExecPin("True"), ExecPin("False")}},
      .emit = [](Emitter& emit) {
        emit.Line("if " + emit.Input("Condition") + " then");
        emit.Indent();
        emit.Chain("True");
        emit.Dedent();
        if (emit.Linked("False")) {
          emit.Line("else");
          emit.Indent();
          emit.Chain("False");
          emit.Dedent();
        }
        emit.Line("end");
      }});

  library.Add({.id = "flow.sequence", .title = "Sequence", .category = category,
      .description = "Runs each output's chain in turn.",
      .pins = {{ExecPin()}, {}},
      .settings = {{.name = "Outputs", .kind = SettingKind::kChoice, .initial = "2", .choices = Counts(2, 8)}},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        for (int i = 1; i <= CountOf(node, "Outputs"); ++i) pins.outputs.push_back(ExecPin("Then " + std::to_string(i)));
      },
      .emit = [](Emitter& emit) {
        for (int i = 1; i <= CountOf(emit.node(), "Outputs"); ++i) emit.Chain("Then " + std::to_string(i));
      }});

  library.Add({.id = "flow.for", .title = "For Loop", .category = category,
      .description = "Runs Body once for each Index from First to Last, then goes on through Completed. Index only exists in Body's chain.",
      .pins = {{ExecPin(), DataPin("First", PinKind::kInt, "1"), DataPin("Last", PinKind::kInt, "10")}, {ExecPin("Body"), DataPin("Index", PinKind::kInt), ExecPin("Completed")}},
      .emit = [](Emitter& emit) {
        emit.Line("for " + emit.Param("Index") + " = " + emit.Input("First") + ", " + emit.Input("Last") + " do");
        emit.Indent();
        emit.Chain("Body");
        emit.Dedent();
        emit.Line("end");
        emit.Chain("Completed");
      }});

  library.Add({.id = "flow.do_once", .title = "Do Once", .category = category,
      .description = "Goes on the first time it's reached, and not again until Reset is reached.",
      .pins = {{ExecPin(), ExecPin("Reset")}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        const std::string done = emit.State("done", "false");
        if (emit.entered() == "Reset") {
          emit.Line(done + " = false");
          emit.EndChain();
          return;
        }
        emit.Line("if not " + done + " then");
        emit.Indent();
        emit.Line(done + " = true");
        emit.Chain(kExecOut);
        emit.Dedent();
        emit.Line("end");
      }});

  library.Add({.id = "flow.flip_flop", .title = "Flip Flop", .category = category,
      .description = "Goes on through A, then B the next time, then A again.",
      .pins = {{ExecPin()}, {ExecPin("A"), ExecPin("B")}},
      .emit = [](Emitter& emit) {
        const std::string a = emit.State("a", "true");
        emit.Line("if " + a + " then");
        emit.Indent();
        emit.Line(a + " = false");
        emit.Chain("A");
        emit.Dedent();
        emit.Line("else");
        emit.Indent();
        emit.Line(a + " = true");
        emit.Chain("B");
        emit.Dedent();
        emit.Line("end");
      }});
}

// Hooks and the guest ------------------------------------------------------------------------------------

void AddGuest(Library& library) {
  const std::string category = "Hooks";

  library.Add({.id = "hook.skip", .title = "Skip Original", .category = category,
      .description = "The hooked function doesn't run this time. The rest of the chain still does. Only in a Before Function chain.",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        if (emit.event().type != "event.hook_before") emit.Error("Skip Original only works in a Before Function event's chain");
        else emit.Line(emit.EventLocal("skip") + " = true");
      }});

  library.Add({.id = "midasm.take", .title = "Take Branch", .category = category,
      .description = "At a midasm branch site, takes the branch. Only in a Midasm Site chain.",
      .pins = {{ExecPin()}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) {
        if (emit.event().type != "event.midasm") emit.Error("Take Branch only works in a Midasm Site event's chain");
        else emit.Line(emit.EventLocal("take") + " = true");
      }});

  const SettingDesc register_setting{.name = "Register", .kind = SettingKind::kChoice, .initial = "r3", .choices = RegisterNames()};

  library.Add({.id = "reg.get", .title = "Get Register", .category = category,
      .description = "Reads a register. r registers, lr and ctr are integers, f registers floats.",
      .role = NodeRole::kPure, .pins = {{DataPin("Registers", PinKind::kRegisters)}, {DataPin("Value", PinKind::kInt)}},
      .settings = {register_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) { Retype(pins.outputs, "Value", RegisterKind(SettingOf(node, "Register"))); },
      .title_of = [](const Node& node) { return Titled(node, "Get ", "Register"); },
      .expression = [](Emitter& emit, std::string_view) { return emit.Input("Registers") + "." + emit.Setting("Register"); }});

  library.Add({.id = "reg.set", .title = "Set Register", .category = category,
      .description = "Writes a register. Before a function, that changes its arguments; after it, what it returns.",
      .pins = {{ExecPin(), DataPin("Registers", PinKind::kRegisters), DataPin("Value", PinKind::kInt)}, {ExecPin(kExecOut)}},
      .settings = {register_setting},
      .shape = [](const Node& node, const Graph&, NodePins& pins) { Retype(pins.inputs, "Value", RegisterKind(SettingOf(node, "Register"))); },
      .title_of = [](const Node& node) { return Titled(node, "Set ", "Register"); },
      .emit = [](Emitter& emit) { emit.Line(emit.Input("Registers") + "." + emit.Setting("Register") + " = " + emit.Input("Value")); }});

  library.Add({.id = "guest.call", .title = "Call Function", .category = category,
      .description = "Calls a guest function with integer arguments and gives back what it returned in r3. Only from a hook or a frame event, on a guest thread.",
      .pins = {{ExecPin(), DataPin("Address", PinKind::kInt, "")}, {ExecPin(kExecOut), DataPin("Result", PinKind::kInt)}},
      .settings = {{.name = "Arguments", .kind = SettingKind::kChoice, .initial = "0", .choices = Counts(0, 8)}},
      .shape = [](const Node& node, const Graph&, NodePins& pins) {
        for (int i = 1; i <= CountOf(node, "Arguments"); ++i) pins.inputs.push_back(DataPin("Arg " + std::to_string(i), PinKind::kInt));
      },
      .emit = [](Emitter& emit) {
        if (emit.event().type == "event.start") emit.Error("The game can't be called from On Start; call it from a hook or a frame event");
        std::vector<std::string> args = {emit.Input("Address")};
        for (int i = 1; i <= CountOf(emit.node(), "Arguments"); ++i) args.push_back(emit.Input("Arg " + std::to_string(i)));
        const std::string call = "rex.call(" + Joined(args) + ")";
        emit.Line(emit.Linked("Result") ? emit.Output("Result") + " = " + call : call);
      }});
}

// Memory -----------------------------------------------------------------------------------------------

void AddMemory(Library& library) {
  const std::string category = "Memory";
  const std::vector<std::string> readable = {"u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64", "string", "wstring"};
  const std::vector<std::string> writable = {"u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64", "string"};

  library.Add({.id = "mem.read", .title = "Read Memory", .category = category,
      .description = "Reads guest memory at Address as Type, from the guest's big endian. wstring reads UTF-16 as text.",
      .role = NodeRole::kPure, .pins = {{DataPin("Address", PinKind::kInt, "")}, {DataPin("Value", PinKind::kInt)}},
      .settings = {{.name = "Type", .kind = SettingKind::kChoice, .initial = "u32", .choices = readable}},
      .shape = [](const Node& node, const Graph&, NodePins& pins) { Retype(pins.outputs, "Value", MemoryKind(SettingOf(node, "Type"))); },
      .title_of = [](const Node& node) { return Titled(node, "Read ", "Type"); },
      .expression = [](Emitter& emit, std::string_view) { return "rex.mem." + emit.Setting("Type") + "(" + emit.Input("Address") + ")"; }});

  library.Add({.id = "mem.write", .title = "Write Memory", .category = category,
      .description = "Writes Value to guest memory at Address as Type, in the guest's big endian.",
      .pins = {{ExecPin(), DataPin("Address", PinKind::kInt, ""), DataPin("Value", PinKind::kInt)}, {ExecPin(kExecOut)}},
      .settings = {{.name = "Type", .kind = SettingKind::kChoice, .initial = "u32", .choices = writable}},
      .shape = [](const Node& node, const Graph&, NodePins& pins) { Retype(pins.inputs, "Value", MemoryKind(SettingOf(node, "Type"))); },
      .title_of = [](const Node& node) { return Titled(node, "Write ", "Type"); },
      .emit = [](Emitter& emit) { emit.Line("rex.mem.set_" + emit.Setting("Type") + "(" + emit.Input("Address") + ", " + emit.Input("Value") + ")"); }});
}

// Variables ----------------------------------------------------------------------------------------------

PinKind VariableKind(const Node& node, const Graph& graph) {
  const Variable* variable = graph.FindVariable(SettingOf(node, "Variable"));
  return variable ? variable->type : PinKind::kAny;
}

// The variable's name, or an error when the graph has no such variable.
std::string VariableName(Emitter& emit) {
  const std::string name = emit.Setting("Variable");
  if (!emit.graph().FindVariable(name)) {
    emit.Error(name.empty() ? "Pick a variable" : "There's no variable named " + name);
    return "nil";
  }
  return name;
}

void AddVariables(Library& library) {
  const std::string category = "Variables";
  const SettingDesc variable{.name = "Variable", .kind = SettingKind::kVariable};

  library.Add({.id = "var.get", .title = "Get", .category = category,
      .description = "A variable's value. Variables keep their values between events.",
      .role = NodeRole::kPure, .pins = {{}, {DataPin("Value", PinKind::kAny)}},
      .settings = {variable},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) { Retype(pins.outputs, "Value", VariableKind(node, graph)); },
      .title_of = [](const Node& node) { return Titled(node, "Get ", "Variable"); },
      .expression = [](Emitter& emit, std::string_view) { return VariableName(emit); }});

  library.Add({.id = "var.set", .title = "Set", .category = category,
      .description = "Gives a variable a new value.",
      .pins = {{ExecPin(), DataPin("Value", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .settings = {variable},
      .shape = [](const Node& node, const Graph& graph, NodePins& pins) { Retype(pins.inputs, "Value", VariableKind(node, graph)); },
      .title_of = [](const Node& node) { return Titled(node, "Set ", "Variable"); },
      .emit = [](Emitter& emit) {
        const std::string name = VariableName(emit);
        if (name != "nil") emit.Line(name + " = " + emit.Input("Value"));
      }});
}

// Math, logic and text ---------------------------------------------------------------------------------

void AddMath(Library& library) {
  const std::string math = "Math";
  const PinKind number = PinKind::kNumber;
  const PinKind integer = PinKind::kInt;
  const PinKind real = PinKind::kFloat;

  library.Add(Binary("math.add", "Add", math, "A + B. Integers in give an integer out.", number, number, "+"));
  library.Add(Binary("math.subtract", "Subtract", math, "A - B.", number, number, "-"));
  library.Add(Binary("math.multiply", "Multiply", math, "A * B.", number, number, "*"));
  library.Add(Binary("math.divide", "Divide", math, "A / B, always a float.", number, real, "/"));
  library.Add(Binary("math.floor_divide", "Floor Divide", math, "A // B: the division rounded down.", number, number, "//"));
  library.Add(Binary("math.modulo", "Modulo", math, "A % B: what's left after dividing.", number, number, "%"));
  library.Add(Pure("math.min", "Min", math, "The smaller of A and B.", {DataPin("A", number), DataPin("B", number)}, DataPin("Result", number), [](Emitter& emit) { return "math.min(" + emit.Input("A") + ", " + emit.Input("B") + ")"; }));
  library.Add(Pure("math.max", "Max", math, "The larger of A and B.", {DataPin("A", number), DataPin("B", number)}, DataPin("Result", number), [](Emitter& emit) { return "math.max(" + emit.Input("A") + ", " + emit.Input("B") + ")"; }));
  library.Add(Pure("math.clamp", "Clamp", math, "Value kept between Min and Max.", {DataPin("Value", number), DataPin("Min", number), DataPin("Max", number, "1")}, DataPin("Result", number), [](Emitter& emit) { return "math.max(" + emit.Input("Min") + ", math.min(" + emit.Input("Max") + ", " + emit.Input("Value") + "))"; }));
  library.Add(Pure("math.lerp", "Lerp", math, "From A at Alpha 0 to B at Alpha 1.", {DataPin("A", real), DataPin("B", real, 1.0), DataPin("Alpha", real, 0.5)}, DataPin("Result", real), [](Emitter& emit) { const std::string a = emit.Input("A"); return "(" + a + " + (" + emit.Input("B") + " - " + a + ") * " + emit.Input("Alpha") + ")"; }));
  library.Add(Unary("math.negate", "Negate", math, "-Value.", number, number, "-"));
  library.Add(Unary("math.abs", "Abs", math, "Value without its sign.", number, number, "math.abs"));
  library.Add(Unary("math.floor", "Floor", math, "Value rounded down to an integer.", number, integer, "math.floor"));
  library.Add(Unary("math.ceil", "Ceil", math, "Value rounded up to an integer.", number, integer, "math.ceil"));
  library.Add(Pure("math.round", "Round", math, "Value rounded to the nearest integer.", {DataPin("Value", number)}, DataPin("Result", integer), [](Emitter& emit) { return "math.floor(" + emit.Input("Value") + " + 0.5)"; }));
  library.Add(Pure("math.to_float", "To Float", math, "An integer as a float.", {DataPin("Value", number)}, DataPin("Result", real), [](Emitter& emit) { return "(" + emit.Input("Value") + " + 0.0)"; }));
  library.Add(Unary("math.sqrt", "Square Root", math, "The square root of Value.", real, real, "math.sqrt"));
  library.Add(Unary("math.sin", "Sin", math, "The sine of Value, in radians.", real, real, "math.sin"));
  library.Add(Unary("math.cos", "Cos", math, "The cosine of Value, in radians.", real, real, "math.cos"));
  library.Add(Pure("math.random", "Random Float", math, "A new random float from 0 up to 1 each time it's read.", {}, DataPin("Result", real), [](Emitter&) { return std::string("math.random()"); }));
  library.Add(Pure("math.random_int", "Random Integer", math, "A new random integer from Min to Max each time it's read.", {DataPin("Min", integer, "1"), DataPin("Max", integer, "6")}, DataPin("Result", integer), [](Emitter& emit) { return "math.random(" + emit.Input("Min") + ", " + emit.Input("Max") + ")"; }));

  const std::string bits = "Math/Bits";
  library.Add(Binary("bits.and", "Bitwise And", bits, "The bits set in both A and B.", integer, integer, "&"));
  library.Add(Binary("bits.or", "Bitwise Or", bits, "The bits set in A or B.", integer, integer, "|"));
  library.Add(Binary("bits.xor", "Bitwise Xor", bits, "The bits set in A or B but not both.", integer, integer, "~"));
  library.Add(Binary("bits.left", "Shift Left", bits, "A's bits moved B places up.", integer, integer, "<<"));
  library.Add(Binary("bits.right", "Shift Right", bits, "A's bits moved B places down.", integer, integer, ">>"));
  library.Add(Unary("bits.not", "Bitwise Not", bits, "Every bit of Value flipped.", integer, integer, "~"));
}

void AddLogic(Library& library) {
  const std::string logic = "Logic";
  const PinKind boolean = PinKind::kBool;
  const PinKind number = PinKind::kNumber;

  library.Add(Binary("logic.and", "And", logic, "True when A and B both are.", boolean, boolean, "and"));
  library.Add(Binary("logic.or", "Or", logic, "True when A or B is.", boolean, boolean, "or"));
  library.Add(Pure("logic.not", "Not", logic, "True when Value is false.", {DataPin("Value", boolean)}, DataPin("Result", boolean), [](Emitter& emit) { return "(not " + emit.Input("Value") + ")"; }));
  library.Add(Binary("logic.equal", "Equal", logic, "True when A and B are the same.", PinKind::kAny, boolean, "=="));
  library.Add(Binary("logic.not_equal", "Not Equal", logic, "True when A and B differ.", PinKind::kAny, boolean, "~="));
  library.Add(Binary("logic.less", "Less", logic, "A < B.", number, boolean, "<"));
  library.Add(Binary("logic.less_equal", "Less Or Equal", logic, "A <= B.", number, boolean, "<="));
  library.Add(Binary("logic.greater", "Greater", logic, "A > B.", number, boolean, ">"));
  library.Add(Binary("logic.greater_equal", "Greater Or Equal", logic, "A >= B.", number, boolean, ">="));
  library.Add(Pure("logic.is_set", "Is Set", logic, "True when Value is anything but nil: an object that exists, a cvar that's there.", {DataPin("Value", PinKind::kAny)}, DataPin("Result", boolean), [](Emitter& emit) { return "(" + emit.Input("Value") + " ~= nil)"; }));
}

void AddText(Library& library) {
  const std::string text = "Text";
  const PinKind string = PinKind::kString;

  library.Add(Pure("text.join", "Join", text, "A followed by B, each turned into text.", {DataPin("A", PinKind::kAny), DataPin("B", PinKind::kAny)}, DataPin("Result", string), [](Emitter& emit) { return "(tostring(" + emit.Input("A") + ") .. tostring(" + emit.Input("B") + "))"; }));
  library.Add(Unary("text.to_text", "To Text", text, "Any value as text.", PinKind::kAny, string, "tostring"));
  library.Add(Pure("text.hex", "Hex", text, "An integer as eight hex digits, like 82558D28.", {DataPin("Value", PinKind::kInt)}, DataPin("Result", string), [](Emitter& emit) { return "string.format(\"%08X\", " + emit.Input("Value") + ")"; }));
  library.Add(Pure("text.length", "Length", text, "How many bytes the text has.", {DataPin("Text", string)}, DataPin("Result", PinKind::kInt), [](Emitter& emit) { return "#" + emit.Input("Text"); }));
  library.Add(Pure("text.contains", "Contains", text, "True when Part is somewhere in Text.", {DataPin("Text", string), DataPin("Part", string)}, DataPin("Result", PinKind::kBool), [](Emitter& emit) { return "(string.find(" + emit.Input("Text") + ", " + emit.Input("Part") + ", 1, true) ~= nil)"; }));
}

// The host: logging, time, input, cvars and switches -------------------------------------------------------

void AddHost(Library& library) {
  library.Add({.id = "log.write", .title = "Log", .category = "Log",
      .description = "Writes Message, and Value when it's given, to the game's log with the mod's name.",
      .pins = {{ExecPin(), DataPin("Message", PinKind::kAny), DataPin("Value", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .settings = {{.name = "Level", .kind = SettingKind::kChoice, .initial = "info", .choices = {"info", "warn", "error", "debug"}}},
      .title_of = [](const Node& node) {
        const std::string level = SettingOf(node, "Level");
        return level == "info" ? std::string("Log") : level == "warn" ? std::string("Log Warning") : level == "error" ? std::string("Log Error") : std::string("Log Debug");
      },
      .emit = [](Emitter& emit) {
        const auto typed = emit.node().values.find("Value");
        const bool value = emit.Linked("Value") || (typed != emit.node().values.end() && typed->second.is_string() && !typed->second.get<std::string>().empty());
        emit.Line("rex." + emit.Setting("Level") + "(" + emit.Input("Message") + (value ? ", " + emit.Input("Value") : std::string()) + ")");
      }});

  library.Add(Pure("time.now", "Now", "Time", "A clock in seconds, for timing things.", {}, DataPin("Seconds", PinKind::kFloat), [](Emitter&) { return std::string("rex.now()"); }));

  const std::string input = "Input";
  library.Add({.id = "input.down", .title = "Key Down", .category = input,
      .description = "True while the key is held.",
      .role = NodeRole::kPure, .pins = {{}, {DataPin("Down", PinKind::kBool)}},
      .settings = {{.name = "Key", .kind = SettingKind::kChoice, .initial = "W", .choices = KeyNames()}},
      .title_of = [](const Node& node) { return Titled(node, "Key ", "Key", " Down"); },
      .expression = [](Emitter& emit, std::string_view) { return "rex.input.down(" + Quoted(emit, "Key") + ")"; }});

  const std::vector<std::string> buttons = {"Left", "Right", "Middle", "Back", "Forward"};
  library.Add({.id = "input.mouse_button", .title = "Mouse Button Down", .category = input,
      .description = "True while the mouse button is held.",
      .role = NodeRole::kPure, .pins = {{}, {DataPin("Down", PinKind::kBool)}},
      .settings = {{.name = "Button", .kind = SettingKind::kChoice, .initial = "Left", .choices = buttons}},
      .title_of = [](const Node& node) { return Titled(node, "Mouse ", "Button", " Down"); },
      .expression = [buttons](Emitter& emit, std::string_view) {
        const auto found = std::find(buttons.begin(), buttons.end(), emit.Setting("Button"));
        return "rex.input.mouse_button(" + std::to_string(found != buttons.end() ? found - buttons.begin() : 0) + ")";
      }});

  library.Add(Pure("input.accepts", "Input Allowed", input, "True while the game window has focus and no menu wants the input. Check it before acting on a held key.", {}, DataPin("Allowed", PinKind::kBool), [](Emitter&) { return std::string("rex.input.accepts()"); }));

  library.Add({.id = "input.mouse_motion", .title = "Read Mouse Motion", .category = input,
      .description = "How far the mouse moved since the last read.",
      .pins = {{ExecPin()}, {ExecPin(kExecOut), DataPin("X", PinKind::kFloat), DataPin("Y", PinKind::kFloat)}},
      .emit = [](Emitter& emit) {
        const std::string x = emit.Output("X");
        const std::string y = emit.Output("Y");
        emit.Line(x + ", " + y + " = rex.input.mouse_delta()");
      }});

  library.Add({.id = "input.wheel", .title = "Read Wheel", .category = input,
      .description = "How far the mouse wheel turned since the last read.",
      .pins = {{ExecPin()}, {ExecPin(kExecOut), DataPin("Amount", PinKind::kFloat)}},
      .emit = [](Emitter& emit) { emit.Line(emit.Output("Amount") + " = rex.input.wheel()"); }});

  library.Add({.id = "input.hold", .title = "Hold Game Input", .category = input,
      .description = "While held, the game doesn't see the keyboard and mouse, for a mod that drives the camera itself.",
      .pins = {{ExecPin(), DataPin("Held", PinKind::kBool, true)}, {ExecPin(kExecOut)}},
      .emit = [](Emitter& emit) { emit.Line("rex.input.hold_guest(" + emit.Input("Held") + ")"); }});

  const std::string settings = "Settings";
  const SettingDesc name{.name = "Name", .kind = SettingKind::kText, .tooltip = "The cvar's name"};

  library.Add({.id = "cvar.get", .title = "Get Setting", .category = settings,
      .description = "A cvar's value: one the game has, or one a mod added.",
      .role = NodeRole::kPure, .pins = {{}, {DataPin("Value", PinKind::kAny)}},
      .settings = {name},
      .title_of = [](const Node& node) { return Titled(node, "Get ", "Name"); },
      .expression = [](Emitter& emit, std::string_view) { return "rex.cvar.get(" + NamedSetting(emit, "Name") + ")"; }});

  library.Add({.id = "cvar.set", .title = "Set Setting", .category = settings,
      .description = "Changes a cvar.",
      .pins = {{ExecPin(), DataPin("Value", PinKind::kAny)}, {ExecPin(kExecOut)}},
      .settings = {name},
      .title_of = [](const Node& node) { return Titled(node, "Set ", "Name"); },
      .emit = [](Emitter& emit) { emit.Line("rex.cvar.set(" + NamedSetting(emit, "Name") + ", " + emit.Input("Value") + ")"); }});

  library.Add({.id = "cvar.define", .title = "Add Setting", .category = settings,
      .description = "Adds a cvar of the mod's own, which shows in the game's settings and is saved like any other. Usually from On Start.",
      .pins = {{ExecPin(), DataPin("Default", PinKind::kBool)}, {ExecPin(kExecOut)}},
      .settings = {name, {.name = "Type", .kind = SettingKind::kChoice, .initial = "boolean", .choices = {"boolean", "int32", "int64", "uint32", "uint64", "number", "string"}},
                   {.name = "Category", .kind = SettingKind::kText, .initial = "Mods"}, {.name = "Description", .kind = SettingKind::kText}},
      .shape = [](const Node& node, const Graph&, NodePins& pins) { Retype(pins.inputs, "Default", CvarKind(SettingOf(node, "Type"))); },
      .title_of = [](const Node& node) { return Titled(node, "Add Setting ", "Name"); },
      .emit = [](Emitter& emit) { emit.Line("rex.cvar." + emit.Setting("Type") + "(" + NamedSetting(emit, "Name") + ", " + emit.Input("Default") + ", " + Quoted(emit, "Category") + ", " + Quoted(emit, "Description") + ")"); }});

  const SettingDesc switch_name{.name = "Name", .kind = SettingKind::kText, .tooltip = "The switch's name, as the game's compiled-in code reads it"};
  library.Add({.id = "switch.get", .title = "Get Switch", .category = settings,
      .description = "A switch the game's compiled-in code reads.",
      .role = NodeRole::kPure, .pins = {{}, {DataPin("Value", PinKind::kInt)}},
      .settings = {switch_name},
      .title_of = [](const Node& node) { return Titled(node, "Get Switch ", "Name"); },
      .expression = [](Emitter& emit, std::string_view) { return "rex.switch.get(" + NamedSetting(emit, "Name") + ")"; }});

  library.Add({.id = "switch.set", .title = "Set Switch", .category = settings,
      .description = "Sets a switch the game's compiled-in code reads. Switches go back to 0 when the scripts reload.",
      .pins = {{ExecPin(), DataPin("Value", PinKind::kInt, "1")}, {ExecPin(kExecOut)}},
      .settings = {switch_name},
      .title_of = [](const Node& node) { return Titled(node, "Set Switch ", "Name"); },
      .emit = [](Emitter& emit) { emit.Line("rex.switch.set(" + NamedSetting(emit, "Name") + ", " + emit.Input("Value") + ")"); }});
}

}  // namespace

void AddBuiltinNodes(Library& library) {
  AddEvents(library);
  AddFlow(library);
  AddGuest(library);
  AddMemory(library);
  AddVariables(library);
  AddMath(library);
  AddLogic(library);
  AddText(library);
  AddHost(library);
}

}  // namespace rexfabric::graph
