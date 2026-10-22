package jsonfmt

import (
	"encoding/json"
	"testing"
)

func set(value string, path ...any) Edit { return Edit{Path: path, Value: json.RawMessage(value)} }

// An edit changes what it names: the rest keeps its numbers as written, its
// keys' order; the whole comes out in the layout.
func TestAnEditChangesOnlyWhatItNames(t *testing.T) {
	in := "{\n  \"n\": 1e-06,\n  \"big\": 1000000000000000,\n  \"e\": [{\"name\": \"a\", \"x\": 1}],\n  \"gone\": true\n}\n"
	got, err := Edited([]byte(in), []Edit{set("2.5", "e", 0.0, "x"), set(`{"name":"b"}`, "e", 1), {Path: []any{"gone"}, Remove: true}, set(`"new"`, "z"), {Path: []any{"none"}, Remove: true}})
	want := "{\n  \"n\": 1e-06,\n  \"big\": 1000000000000000,\n  \"e\": [\n    {\n      \"name\": \"a\",\n      \"x\": 2.5\n    },\n    {\n      \"name\": \"b\"\n    }\n  ],\n  \"z\": \"new\"\n}\n"
	if err != nil || string(got) != want {
		t.Errorf("%v\n%s", err, got)
	}
	if same, _ := Edited([]byte(in), nil); string(same) == in {
		t.Error("not as the layout writes it, yet unchanged")
	}
	if canon, _ := Format([]byte(want)); string(canon) != want {
		t.Errorf("the layout's own text changed:\n%s", canon)
	}
}

// An edit of something that isn't there says so.
func TestAnEditOfNothingIsAnError(t *testing.T) {
	in := []byte(`{"e": [1], "s": "x"}`)
	for _, e := range []Edit{set("1", "no", "x"), set("1", "e", 2.0), set("1", "e", 0.5), set("1", "e", "k"), set("1", "s", "k"), {Path: []any{}, Remove: true}, set("1", "e", 1.0, "x")} {
		if _, err := Edited(in, []Edit{e}); err == nil {
			t.Errorf("%v made", e.Path)
		}
	}
}
