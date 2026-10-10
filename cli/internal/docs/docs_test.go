package docs

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// The embedded copies match the repo's docs/: run `go generate ./internal/docs` after editing those.
func TestCopiesMatchTheRepo(t *testing.T) {
	for _, topic := range Topics() {
		want, err := os.ReadFile(filepath.Join("..", "..", "..", "docs", topic.Name+".md"))
		if err != nil {
			t.Fatal(err)
		}
		if got, _ := Read(topic.Name); got != string(want) {
			t.Errorf("md/%s.md is stale: run go generate ./internal/docs", topic.Name)
		}
	}
}

func TestTopicsHaveTitles(t *testing.T) {
	topics := Topics()
	if len(topics) < 5 {
		t.Fatalf("topics: %v", topics)
	}
	for _, topic := range topics {
		if topic.Title == "" {
			t.Errorf("%s has no title", topic.Name)
		}
	}
}

func TestAgentGuideNamesTheProject(t *testing.T) {
	g := AgentGuide("Space Rocks")
	if !strings.HasPrefix(g, "# Space Rocks:") || strings.Contains(g, "{{") {
		t.Fatalf("guide starts: %q", g[:60])
	}
}

func TestUnknownTopicListsTheKnownOnes(t *testing.T) {
	if _, err := Read("nope"); err == nil || !strings.Contains(err.Error(), "scripting") {
		t.Fatalf("err = %v", err)
	}
}

// The site turns each h2/h3 into an anchor from its text (site/build.mjs slugify), so two alike would share one.
func TestHeadingAnchorsAreUnique(t *testing.T) {
	fence := regexp.MustCompile("(?s)```.*?```")
	heading := regexp.MustCompile(`(?m)^#{2,3} (.+)$`)
	strip := regexp.MustCompile(`<[^>]+>|&[a-z]+;|[^\w\s-]`)
	for _, topic := range Topics() {
		md, _ := Read(topic.Name)
		seen := map[string]bool{}
		for _, m := range heading.FindAllStringSubmatch(fence.ReplaceAllString(md, ""), -1) {
			slug := strings.Join(strings.Fields(strip.ReplaceAllString(strings.ToLower(m[1]), "")), "-")
			if seen[slug] {
				t.Errorf("%s: two headings make the anchor #%s", topic.Name, slug)
			}
			seen[slug] = true
		}
	}
}
