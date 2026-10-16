package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
)

// The modern protocol (2026-07-28): no handshake, every request says its
// version and the client's capabilities in _meta, and a tool asks the person
// by ending its call with input_required (MRTR). The client asks, then calls
// again with the answers; the tool runs again from the start, and each form
// it shows is answered from them. Requests without the version are legacy
// (initialize), as before.

const mcpModernVersion = "2026-07-28"

// requestMeta is the modern _meta every request carries.
type requestMeta struct {
	Version string     `json:"io.modelcontextprotocol/protocolVersion"`
	Caps    clientCaps `json:"io.modelcontextprotocol/clientCapabilities"`
}

func metaOf(params json.RawMessage) requestMeta {
	var p struct {
		Meta requestMeta `json:"_meta"`
	}
	_ = json.Unmarshal(params, &p)
	return p.Meta
}

// formRound is one modern tool call's forms: the answers the client brought
// back (this round's and, through requestState, the earlier ones'), and the
// form the call stopped at, if it did.
type formRound struct {
	call    string // what the answers are for: the tool, its arguments and the open game
	forms   formSupport
	answers map[string]json.RawMessage
	asked   int
	pending map[string]any // the input_required result
}

// roundState is requestState: the person's answers so far, and the call
// they're for (another call, or the same with another game open, can't take them).
type roundState struct {
	Call    string                     `json:"call"`
	Answers map[string]json.RawMessage `json:"answers"`
}

// newRound reads a modern tools/call's answers; an error when its requestState isn't this call's.
func newRound(forms formSupport, params json.RawMessage) (*formRound, error) {
	var p struct {
		Name           string                     `json:"name"`
		Arguments      json.RawMessage            `json:"arguments"`
		InputResponses map[string]json.RawMessage `json:"inputResponses"`
		RequestState   string                     `json:"requestState"`
	}
	_ = json.Unmarshal(params, &p)
	var args any
	_ = json.Unmarshal(p.Arguments, &args)
	canonical, _ := json.Marshal(args) // the retry's arguments, whatever their key order
	game, _ := os.Getwd()
	r := &formRound{call: p.Name + " " + string(canonical) + " " + game, forms: forms, answers: map[string]json.RawMessage{}}
	if p.RequestState != "" {
		var state roundState
		data, err := base64.RawURLEncoding.DecodeString(p.RequestState)
		if err != nil || json.Unmarshal(data, &state) != nil || state.Call != r.call {
			return nil, fmt.Errorf("requestState isn't this call's (or the open game changed since): call it again")
		}
		for key, v := range state.Answers { // only the person's own answers: a forged one is just another answer
			r.answers[key] = v
		}
	}
	for key, v := range p.InputResponses {
		r.answers[key] = v
	}
	return r, nil
}

// ask is the answer to this call's next form, when the client brought it;
// else it records the form for the call to end with.
func (r *formRound) ask(message string, schema map[string]any) (map[string]any, formOutcome) {
	if r.forms == noForms || r.pending != nil {
		return nil, formUnavailable
	}
	r.asked++
	key := fmt.Sprintf("form%d", r.asked)
	if answer, ok := r.answers[key]; ok {
		var reply struct {
			Action  string         `json:"action"`
			Content map[string]any `json:"content"`
		}
		if json.Unmarshal(answer, &reply) != nil || reply.Action != "accept" {
			return nil, formDeclined
		}
		if reply.Content == nil {
			reply.Content = map[string]any{}
		}
		return reply.Content, formAnswered
	}
	params := map[string]any{"mode": "form", "message": message, "requestedSchema": plainSchema(schema)}
	if r.forms == openaiForms { // ChatGPT's form rides in _meta, MCP's own left empty
		params["requestedSchema"] = object(map[string]any{})
		params["_meta"] = map[string]any{"openai/elicitation": map[string]any{"requestedSchema": schema}}
	}
	state, _ := json.Marshal(roundState{Call: r.call, Answers: r.answers})
	r.pending = map[string]any{"resultType": "input_required",
		"inputRequests": map[string]any{key: map[string]any{"method": "elicitation/create", "params": params}},
		"requestState":  base64.RawURLEncoding.EncodeToString(state)}
	return nil, formPending
}

// modernStatus is the HTTP status of a modern request's error reply (200: none, or legacy).
func modernStatus(body []byte, reply rpcMessage) int {
	var req struct {
		Params json.RawMessage `json:"params"`
	}
	if reply.Error == nil || json.Unmarshal(body, &req) != nil || metaOf(req.Params).Version == "" {
		return 200
	}
	switch reply.Error.Code {
	case -32601:
		return 404
	case -32600, -32602, -32022:
		return 400
	}
	return 200
}
