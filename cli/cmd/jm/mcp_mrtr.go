package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"slices"
	"strings"
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
	Version string
	Caps    clientCaps
}

// metaOf is a request's modern _meta (Version "": legacy), or why it's malformed.
func metaOf(params json.RawMessage) (requestMeta, error) {
	var p struct {
		Meta map[string]json.RawMessage `json:"_meta"`
	}
	_ = json.Unmarshal(params, &p)
	var meta requestMeta
	version, ok := p.Meta["io.modelcontextprotocol/protocolVersion"]
	if !ok {
		return meta, nil
	}
	if !strings.HasPrefix(string(version), `"`) || json.Unmarshal(version, &meta.Version) != nil { // null too
		return meta, errors.New("_meta's io.modelcontextprotocol/protocolVersion must be a string")
	}
	caps := p.Meta["io.modelcontextprotocol/clientCapabilities"]
	if meta.Version == mcpModernVersion && (!strings.HasPrefix(strings.TrimSpace(string(caps)), "{") || json.Unmarshal(caps, &meta.Caps) != nil) {
		return meta, errors.New("_meta needs io.modelcontextprotocol/clientCapabilities, an object")
	}
	return meta, nil
}

// formRound is one modern tool call's forms: the answers the client brought
// back (this round's and, through requestState, the earlier ones'), and the
// form the call stopped at, if it did.
type formRound struct {
	call    string // what the answers are for: the tool, its arguments and the open game (bind)
	forms   formSupport
	state   *roundState // the earlier rounds', verified as this server's
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

// stateKey signs requestState: a client can bring it back, not change it.
var stateKey = func() []byte {
	key := make([]byte, 32)
	_, _ = rand.Read(key)
	return key
}()

func signState(state roundState) string {
	data, _ := json.Marshal(state)
	mac := hmac.New(sha256.New, stateKey)
	mac.Write(data)
	return base64.RawURLEncoding.EncodeToString(data) + "." + base64.RawURLEncoding.EncodeToString(mac.Sum(nil))
}

func openState(token string) (*roundState, bool) {
	data64, sig64, _ := strings.Cut(token, ".")
	data, err1 := base64.RawURLEncoding.DecodeString(data64)
	sig, err2 := base64.RawURLEncoding.DecodeString(sig64)
	mac := hmac.New(sha256.New, stateKey)
	mac.Write(data)
	var state roundState
	if err1 != nil || err2 != nil || !hmac.Equal(sig, mac.Sum(nil)) || json.Unmarshal(data, &state) != nil {
		return nil, false
	}
	return &state, true
}

var errStaleState = errors.New("requestState isn't this call's (or the open game changed since): call it again")

// newRound reads a modern tools/call's answers; an error when its requestState isn't this server's.
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
	r := &formRound{call: p.Name + " " + string(canonical), forms: forms, answers: map[string]json.RawMessage{}}
	if p.RequestState != "" {
		var ok bool
		if r.state, ok = openState(p.RequestState); !ok {
			return nil, errStaleState
		}
	}
	for key, v := range p.InputResponses {
		r.answers[key] = v
	}
	return r, nil
}

// bind ties the round to the game the call runs in (under the calls lock), taking
// its earlier answers only when they're for this very call.
func (r *formRound) bind(game string) error {
	r.call += " " + game
	if r.state == nil {
		return nil
	}
	if r.state.Call != r.call {
		return errStaleState
	}
	for key, v := range r.state.Answers {
		if _, now := r.answers[key]; !now {
			r.answers[key] = v
		}
	}
	return nil
}

// ask is the answer to this call's next form, when the client brought one
// that fills it; else it records the form for the call to end with.
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
		err := json.Unmarshal(answer, &reply)
		switch {
		case err == nil && (reply.Action == "decline" || reply.Action == "cancel"):
			return nil, formDeclined
		case err == nil && reply.Action == "accept" && fills(reply.Content, r.shown(schema)):
			return reply.Content, formAnswered
		}
		delete(r.answers, key) // not an answer to this form: asked again
	}
	params := map[string]any{"mode": "form", "message": message, "requestedSchema": r.shown(schema)}
	if r.forms == openaiForms { // ChatGPT's form rides in _meta, MCP's own left empty
		params["requestedSchema"] = object(map[string]any{})
		params["_meta"] = map[string]any{"openai/elicitation": map[string]any{"requestedSchema": schema}}
	}
	r.pending = map[string]any{"resultType": "input_required",
		"inputRequests": map[string]any{key: map[string]any{"method": "elicitation/create", "params": params}},
		"requestState":  signState(roundState{Call: r.call, Answers: r.answers})}
	return nil, formPending
}

// shown is the form of schema this client is shown: ChatGPT's superset, or MCP's plain one.
func (r *formRound) shown(schema map[string]any) map[string]any {
	if r.forms == openaiForms {
		return schema
	}
	return plainSchema(schema)
}

// fills says whether content has every field schema requires, and each field
// it has is of its type, among its choices where it has some.
func fills(content map[string]any, schema map[string]any) bool {
	props, _ := schema["properties"].(map[string]any)
	required, _ := schema["required"].([]string)
	for _, field := range required {
		v, ok := content[field]
		if list, isList := v.([]any); !ok || v == nil || v == "" || isList && len(list) == 0 {
			return false
		}
	}
	for field, v := range content {
		if prop, ok := props[field].(map[string]any); ok && !fits(v, prop) {
			return false
		}
	}
	return true
}

func fits(v any, prop map[string]any) bool {
	var choices []string
	for _, c := range choicesOf(prop) {
		choices = append(choices, c.value)
	}
	if enum, ok := prop["enum"].([]string); ok { // a plain form's choices
		choices = enum
	}
	chosen := func(x any) bool {
		s, ok := x.(string)
		return ok && (choices == nil || slices.Contains(choices, s))
	}
	switch prop["type"] {
	case "string":
		return chosen(v)
	case "array":
		items, ok := v.([]any)
		for _, x := range items {
			ok = ok && chosen(x)
		}
		return ok
	case "boolean":
		_, ok := v.(bool)
		return ok
	case "integer", "number":
		_, ok := v.(float64)
		return ok
	}
	return true
}

// modernStatus is the HTTP status of a modern request's error reply (200: none, or legacy).
func modernStatus(body []byte, reply rpcMessage) int {
	var req struct {
		Params json.RawMessage `json:"params"`
	}
	if reply.Error == nil || json.Unmarshal(body, &req) != nil {
		return 200
	}
	if meta, err := metaOf(req.Params); meta.Version == "" && err == nil {
		return 200
	}
	switch reply.Error.Code {
	case -32601:
		return 404
	case -32600, -32602, -32020, -32022:
		return 400
	}
	return 200
}
