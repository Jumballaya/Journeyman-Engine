package main

import (
	"embed"
	"io/fs"
	"strings"
)

// Journeyman Studio: one page of tabs beside the editor (the games, the open
// game's overview, …). Each tab is a widget/studio/*.js file, inlined at the
// page's __JM_STUDIO__ comment in file name order.

//go:embed widget/studio.html
var studioShell string

//go:embed widget/studio/*.js
var studioTabFiles embed.FS

var studio = widget{name: "journeyman-studio", title: "Journeyman Studio", html: studioPage(), modes: []string{"fullscreen", "inline"},
	description: "Journeyman Studio, beside the editor: the person's games, and the open game's scenes, plays and more in tabs."}

func studioPage() string {
	var parts []string
	files, _ := fs.Glob(studioTabFiles, "widget/studio/*.js") // sorted
	for _, name := range files {
		data, _ := studioTabFiles.ReadFile(name)
		parts = append(parts, string(data))
	}
	return strings.Replace(studioShell, "/*__JM_STUDIO__*/", strings.Join(parts, "\n"), 1)
}

func (s *mcpServer) studioTools() []mcpTool {
	home := gamesHome()
	return []mcpTool{{Name: "studio", Title: "Journeyman Studio",
		Description: "Shows the person Journeyman Studio (their games; the open game's scenes, plays and more, in tabs) at a page: " +
			"use it to show them something, e.g. route \"/game\". Gives you the games, as `games` does.",
		Annotations: readOnly(), InputSchema: object(map[string]any{"route": strArg(`a Studio page, as its deep links write it: "/" (the games), "/game" (the open game's overview), ` +
			`or another tab's, e.g. "/scene?scene=scenes/main.scene.json"; game=<path> in the query opens that game first`)}),
		Meta: studio.toolMeta("Opening the Studio…", "Journeyman Studio", entrypoint("thread")),
		run:  func(toolArgs) toolResult { return listGames(home) }}}
}
