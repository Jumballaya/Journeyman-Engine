package schema

import (
	"fmt"
	"strings"
	"testing"
)

const testSchema = `{"schemaVersion": 1, "components": {
  "TransformComponent": {"label": "Transform", "category": "Core", "summary": "", "scriptFields": [],
    "fields": [{"key": "position", "kind": "vec3", "default": [0, 0, 0]}, {"key": "scale", "kind": "vec2", "default": [1, 1]}]},
  "BoxColliderComponent": {"label": "Box Collider", "category": "Physics", "summary": "", "scriptFields": [],
    "fields": [{"key": "halfExtents", "kind": "vec2", "default": [8, 8]}, {"key": "layerMask", "kind": "mask", "default": 1}]},
  "AudioEmitterComponent": {"label": "Audio Emitter", "category": "Audio", "summary": "", "scriptFields": [],
    "fields": [{"key": "bus", "kind": "choice", "default": "sfx", "choices": ["sfx", "music"]},
               {"key": "shadow", "kind": "group", "default": null, "fields": [{"key": "x", "kind": "number", "default": 0}]}]},
  "LocalTransformComponent": {"label": "", "category": "", "summary": "", "fields": [], "scriptFields": []}
}}`

func check(t *testing.T, doc string) []string {
	t.Helper()
	s, err := Parse([]byte(testSchema))
	if err != nil {
		t.Fatal(err)
	}
	return lines(s.Check("scenes/a.scene.json", []byte(doc)))
}

func lines(problems []Problem) []string {
	out := []string{}
	for _, p := range problems {
		out = append(out, p.String())
	}
	return out
}

func TestCleanContentHasNoProblems(t *testing.T) {
	problems := check(t, `{"entities": [
	  {"name": "Hero", "components": {"TransformComponent": {"position": [1, 2, 3]}, "BoxColliderComponent": {"layerMask": 4}},
	   "children": [{"name": "Shadow", "components": {"TransformComponent": {"scale": [2, 2]}}}]},
	  {"name": "Bat", "prefab": "assets/prefabs/bat.prefab.json", "overrides": {"TransformComponent": {"position": [0, 0, 1]}}},
	  {"name": "Music", "components": {"AudioEmitterComponent": {"bus": "music", "shadow": {"x": 2}}, "LocalTransformComponent": {"x": 1}}}
	]}`)
	if len(problems) != 0 {
		t.Fatalf("expected none, got %q", problems)
	}
}

func TestUnknownNamesGetASuggestion(t *testing.T) {
	problems := check(t, `{"entities": [{"name": "Wall", "components": {
	  "BoxCollider": {}, "TransformComponent": {"postion": [0, 0, 0]}}}]}`)
	want := []string{
		`scenes/a.scene.json: "Wall": unknown component "BoxCollider" (did you mean "BoxColliderComponent"?)`,
		`scenes/a.scene.json: "Wall" TransformComponent: unknown key "postion" (did you mean "position"?) (keys: position, scale)`,
	}
	if strings.Join(problems, "\n") != strings.Join(want, "\n") {
		t.Fatalf("got\n%s\nwant\n%s", strings.Join(problems, "\n"), strings.Join(want, "\n"))
	}
}

func TestWrongKindsAreReported(t *testing.T) {
	problems := check(t, `{"entities": [{"name": "Hero", "components": {
	  "TransformComponent": {"position": [1, 2]},
	  "BoxColliderComponent": {"layerMask": -1},
	  "AudioEmitterComponent": {"bus": "voice", "shadow": {"x": "far"}}}}]}`)
	for _, want := range []string{
		`"Hero" TransformComponent.position: expected [x, y, z], got [1,2]`,
		`"Hero" BoxColliderComponent.layerMask: expected a layer mask`,
		`"Hero" AudioEmitterComponent.bus: expected one of sfx, music, got "voice"`,
		`"Hero" AudioEmitterComponent.shadow.x: expected a number, got "far"`,
	} {
		if !strings.Contains(strings.Join(problems, "\n"), want) {
			t.Errorf("missing %q in\n%s", want, strings.Join(problems, "\n"))
		}
	}
}

// Prefabs, their children, and instances' overrides of children by name.
func TestPrefabsAndChildOverrides(t *testing.T) {
	s, _ := Parse([]byte(testSchema))
	problems := lines(s.Check("assets/prefabs/knight.prefab.json", []byte(`{
	  "components": {"TransformComponent": {"scael": [1, 1]}},
	  "children": [{"name": "Sword", "components": {"Sprite": {}}}],
	  "overrides": {"children": {"Sword": {"TransformComponent": {"position": "up"}}}}}`)))
	want := []string{
		`assets/prefabs/knight.prefab.json: prefab TransformComponent: unknown key "scael" (did you mean "scale"?) (keys: position, scale)`,
		`assets/prefabs/knight.prefab.json: prefab > Sword TransformComponent.position: expected [x, y, z], got "up"`,
		`assets/prefabs/knight.prefab.json: "Sword": unknown component "Sprite"`,
	}
	if strings.Join(problems, "\n") != strings.Join(want, "\n") {
		t.Fatalf("got\n%s\nwant\n%s", strings.Join(problems, "\n"), strings.Join(want, "\n"))
	}
}

func TestANewerSchemaVersionIsRefused(t *testing.T) {
	if _, err := Parse([]byte(`{"schemaVersion": 2, "components": {}}`)); err == nil {
		t.Fatal("expected an error")
	}
}

func TestProblemsSayWhereTheKeyIs(t *testing.T) {
	s, err := Parse([]byte(testSchema))
	if err != nil {
		t.Fatal(err)
	}
	doc := `{"entities": [
  {"name": "Hero", "components": {"TransformComponent": {"position": [0, 0, 0]}}},
  {"name": "Wall", "components": {
    "BoxCollider": {},
    "TransformComponent": {"scale": [1, 1], "postion": [0, 0, 0]}}}
]}`
	got := []string{}
	for _, p := range s.Check("scenes/a.scene.json", []byte(doc)) {
		got = append(got, fmt.Sprintf("%d:%d %s", p.Line, p.Column, p.Message))
	}
	want := []string{
		`4:5 unknown component "BoxCollider" (did you mean "BoxColliderComponent"?)`,
		`5:45 unknown key "postion" (did you mean "position"?) (keys: position, scale)`,
	}
	if strings.Join(got, "\n") != strings.Join(want, "\n") {
		t.Fatalf("got\n%s", strings.Join(got, "\n"))
	}
}
