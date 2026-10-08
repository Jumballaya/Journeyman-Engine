package jsonfmt

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// testdata/<name>.in.json formats to <name>.out.json; the editor's C++ writer
// is tested against the same files.
func TestFixtures(t *testing.T) {
	inputs, _ := filepath.Glob("testdata/*.in.json")
	if len(inputs) == 0 {
		t.Fatal("no fixtures")
	}
	for _, in := range inputs {
		data, _ := os.ReadFile(in)
		want, _ := os.ReadFile(strings.Replace(in, ".in.json", ".out.json", 1))
		got, err := Format(data)
		if err != nil {
			t.Fatalf("%s: %v", in, err)
		}
		if string(got) != string(want) {
			t.Errorf("%s:\n got:\n%s\nwant:\n%s", in, got, want)
		}
		again, _ := Format(got)
		if string(again) != string(got) {
			t.Errorf("%s: formatting the output changes it", in)
		}
	}
}

func TestShortestMatchesNlohmann(t *testing.T) {
	for v, want := range map[float64]string{
		0.5: "0.5", -12.25: "-12.25", 1234567.5: "1234567.5", 0.001: "0.001", 0.00001: "1e-05",
		1.5e21: "1.5e+21", 1e15: "1e+15", 123456789012345.5: "123456789012345.5",
	} {
		if got := shortest(v); got != want {
			t.Errorf("shortest(%v) = %s, want %s", v, got, want)
		}
	}
}

func TestBadJSONIsAnError(t *testing.T) {
	for _, in := range []string{`{"a": }`, `{"a": 1} x`, ``} {
		if _, err := Format([]byte(in)); err == nil {
			t.Errorf("%q: expected an error", in)
		}
	}
}

// An edit through a Go map sorts keys; the file keeps its order.
func TestFormatKeepingRestoresTheFilesKeyOrder(t *testing.T) {
	previous := []byte(`{"name": "Game", "version": "1", "scenes": ["a"], "config": {"window": {"width": 640, "height": 480}}}`)
	edited := []byte(`{"config": {"window": {"height": 480, "width": 640}}, "entryScene": "b", "name": "Game", "scenes": ["a", "b"], "version": "1"}`)
	got, err := FormatKeeping(edited, previous)
	if err != nil {
		t.Fatal(err)
	}
	want := `{
  "name": "Game",
  "version": "1",
  "scenes": ["a", "b"],
  "config": {
    "window": {
      "width": 640,
      "height": 480
    }
  },
  "entryScene": "b"
}
`
	if string(got) != want {
		t.Fatalf("got\n%s", got)
	}
}
