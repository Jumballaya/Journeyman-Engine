package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strconv"
	"sync"
)

// mcpPeer is the client at the other end of stdio, which the server can ask
// things too (a form while a tool runs): it writes every message and matches
// the client's replies to the requests waiting for them.
type mcpPeer struct {
	mu      sync.Mutex
	out     io.Writer
	next    int
	waiting map[string]chan rpcMessage
	closed  bool
}

func newMCPPeer(out io.Writer) *mcpPeer {
	return &mcpPeer{out: out, waiting: map[string]chan rpcMessage{}}
}

func (p *mcpPeer) send(msg any) {
	data, _ := json.Marshal(msg)
	p.mu.Lock()
	defer p.mu.Unlock()
	fmt.Fprintf(p.out, "%s\n", data)
}

var errClientGone = errors.New("the client is gone")

// start asks the client; its reply comes on the channel, which closes if the
// client's input ends first.
func (p *mcpPeer) start(method string, params any) <-chan rpcMessage {
	p.mu.Lock()
	reply := make(chan rpcMessage, 1)
	if p.closed {
		p.mu.Unlock()
		close(reply)
		return reply
	}
	p.next++
	id := `"jm-` + strconv.Itoa(p.next) + `"`
	p.waiting[id] = reply
	p.mu.Unlock()
	data, _ := json.Marshal(params)
	p.send(rpcMessage{JSONRPC: "2.0", ID: json.RawMessage(id), Method: method, Params: data})
	return reply
}

// decode is a reply's result in v: errClientGone when there was none, else
// the client's error.
func decode(msg rpcMessage, ok bool, v any) error {
	if !ok {
		return errClientGone
	}
	if msg.Error != nil {
		return errors.New(msg.Error.Message)
	}
	data, _ := json.Marshal(msg.Result)
	return json.Unmarshal(data, v)
}

// deliver hands a reply to the request waiting for it; false: data isn't one.
func (p *mcpPeer) deliver(data []byte) bool {
	var msg rpcMessage
	if json.Unmarshal(data, &msg) != nil || msg.Method != "" || msg.ID == nil {
		return false
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if reply, ok := p.waiting[string(msg.ID)]; ok {
		delete(p.waiting, string(msg.ID))
		reply <- msg
	}
	return true
}

// abandon gives up on the request reply waits for, telling the client so.
func (p *mcpPeer) abandon(reply <-chan rpcMessage) {
	p.mu.Lock()
	var abandoned json.RawMessage
	for id, waiting := range p.waiting {
		if waiting == reply {
			abandoned = json.RawMessage(id)
			delete(p.waiting, id)
		}
	}
	p.mu.Unlock()
	if abandoned != nil {
		params, _ := json.Marshal(map[string]any{"requestId": abandoned, "reason": "the tool call it was for was cancelled"})
		p.send(rpcMessage{JSONRPC: "2.0", Method: "notifications/cancelled", Params: params})
	}
}

// close fails every request still waiting: the client's input ended.
func (p *mcpPeer) close() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.closed = true
	for id, reply := range p.waiting {
		close(reply)
		delete(p.waiting, id)
	}
}
