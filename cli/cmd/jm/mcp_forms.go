package main

import "strings"

// Forms: a tool can ask the person something while it runs, in a form the
// host draws. ChatGPT's (openai/elicitation/create) has pictures, option
// descriptions, suggestions and file pickers; plain MCP clients get the same
// form flattened (elicitation/create); hosts with neither get no form, and the
// tool goes on from its arguments.

type formSupport int

const (
	noForms formSupport = iota
	plainForms
	openaiForms
)

// clientCaps are the client capabilities forms depend on.
type clientCaps struct {
	Elicitation map[string]any `json:"elicitation"`
	Extensions  map[string]struct {
		Form map[string]any `json:"form"`
	} `json:"extensions"`
}

// formsOf is what forms a client with caps shows. Over the modern protocol
// (MRTR) ChatGPT's need MCP's elicitation too.
func formsOf(caps clientCaps, modern bool) formSupport {
	_, form := caps.Elicitation["form"]
	plain := caps.Elicitation != nil && (len(caps.Elicitation) == 0 || form) // {} (2025-06-18) is forms
	switch {
	case caps.Extensions["openai/elicitation"].Form != nil && (plain || !modern):
		return openaiForms
	case plain:
		return plainForms
	}
	return noForms
}

type formOutcome int

const (
	formUnavailable formOutcome = iota // the host shows no forms: go on without
	formDeclined                       // the person closed it
	formAnswered
	formPending // asked over MRTR: the call ends, and comes again with the answer
)

// showsForms says whether the client of the call running can show forms.
func (s *mcpServer) showsForms() bool {
	if s.round != nil {
		return s.round.forms != noForms
	}
	return s.peer != nil && s.forms != noForms
}

// ask shows the person a form of fields (a JSON object schema in ChatGPT's
// superset) and gives their answer, keyed as the fields are.
func (s *mcpServer) ask(message string, fields map[string]any, required ...string) (map[string]any, formOutcome) {
	if s.round != nil {
		return s.round.ask(message, object(fields, required...))
	}
	if s.peer == nil || s.forms == noForms {
		return nil, formUnavailable
	}
	schema := object(fields, required...)
	method, params := "openai/elicitation/create", map[string]any{"mode": "form", "message": message, "requestedSchema": schema}
	if s.forms == plainForms {
		method, params = "elicitation/create", map[string]any{"message": message, "requestedSchema": plainSchema(schema)}
	}
	var reply struct {
		Action  string         `json:"action"`
		Content map[string]any `json:"content"`
	}
	msg, ok := s.await(s.peer.start(method, params))
	if err := decode(msg, ok, &reply); err == errClientGone {
		return nil, formDeclined
	} else if err != nil {
		return nil, formUnavailable // it couldn't after all (unsupported fields): as if it had no forms
	}
	if reply.Action != "accept" {
		return nil, formDeclined
	}
	if reply.Content == nil {
		reply.Content = map[string]any{}
	}
	return reply.Content, formAnswered
}

// plainSchema is a form in MCP's own terms: choices (options, pickers) become
// enums of one value, ChatGPT's extras go.
func plainSchema(schema map[string]any) map[string]any {
	out := map[string]any{}
	for key, v := range schema {
		out[key] = v
	}
	fields := map[string]any{}
	for key, f := range schema["properties"].(map[string]any) {
		fields[key] = plainField(f.(map[string]any))
	}
	out["properties"] = fields
	return out
}

func plainField(f map[string]any) map[string]any {
	out := map[string]any{}
	for key, v := range f {
		if !strings.HasPrefix(key, "x-openai") && key != "oneOf" && key != "pattern" && key != "items" {
			out[key] = v
		}
	}
	var values, names []string
	for _, c := range choicesOf(f) {
		values, names = append(values, c.value), append(names, c.title)
	}
	if len(values) > 0 {
		out["type"], out["enum"], out["enumNames"] = "string", values, names
		if list, ok := out["default"].([]string); ok { // a multi-select's: one value here
			delete(out, "default")
			if len(list) > 0 {
				out["default"] = list[0]
			}
		}
	}
	return out
}

type formChoice struct{ value, title string }

// choicesOf are a field's options: titled consts, or a picker's resources.
func choicesOf(f map[string]any) []formChoice {
	var out []formChoice
	options, _ := f["oneOf"].([]map[string]any)
	for _, o := range options {
		out = append(out, formChoice{o["const"].(string), o["title"].(string)})
	}
	if input, ok := f["x-openai-input"].(map[string]any); ok {
		resources, _ := input["options"].([]map[string]any)
		for _, r := range resources {
			out = append(out, formChoice{r["uri"].(string), r["name"].(string)})
		}
	}
	return out
}

// option is a titled choice; thumb (a data: URI) shows it as a picture.
func option(value, title, description, thumb string) map[string]any {
	o := map[string]any{"const": value, "title": title}
	if description != "" {
		o["description"] = description
	}
	if thumb != "" {
		o["x-openai-thumbnail"] = map[string]any{"src": thumb}
	}
	return o
}

func choiceField(title string, fallback string, options ...map[string]any) map[string]any {
	return map[string]any{"type": "string", "title": title, "oneOf": options, "default": fallback}
}

// answers lists a field's answer: one string, or a multi-select's strings
// (a plain form's single choice for it too).
func answers(v any) []string {
	if s, ok := v.(string); ok && s != "" {
		return []string{s}
	}
	var out []string
	for _, item := range asList(v) {
		if s, ok := item.(string); ok {
			out = append(out, s)
		}
	}
	return out
}
