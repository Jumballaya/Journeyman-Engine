package plugin

// Companions: off-the-shelf tools people make games with. The person turns
// them on (jm companions); each then brings a skill teaching the work across
// it and Journeyman, and its own MCP server where it has one.

// A Companion is a tool turned on, as the plugin runs it.
type Companion struct {
	Name   string
	Server *Server // nil: no MCP server, the skill only
}

// Server is a stdio MCP server, as Agent Plugins' mcp.json has it (no
// secrets: the file has no way to take them from the person's environment).
type Server struct {
	Command string
	Args    []string
	Env     map[string]string
}

func (s Server) json() map[string]any {
	out := map[string]any{"type": "stdio", "command": s.Command, "args": s.Args}
	if len(s.Env) > 0 {
		out["env"] = s.Env
	}
	return out
}
