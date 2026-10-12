# Issue tracker: GitHub

Issues and specs live in GitHub Issues for `ajare/vulkan-poly-pusher`.
Use the `gh` CLI from this clone; it infers the repository from the remote.

## Conventions

- Create: `gh issue create --title "..." --body "..."`
  Use a heredoc for multi-line bodies.
- Read: `gh issue view <number> --comments`
- List: `gh issue list --state open --json number,title,body,labels,comments`
  Add label and state filters as needed.
- Comment: `gh issue comment <number> --body "..."`
- Label: `gh issue edit <number> --add-label "..."`
- Remove label: `gh issue edit <number> --remove-label "..."`
- Close: `gh issue close <number> --comment "..."`

## Pull requests as a triage surface

**PRs as a request surface: no.**

## Skill terminology

- "Publish to the issue tracker": create a GitHub issue.
- "Fetch the relevant ticket": run `gh issue view <number> --comments`.

## Wayfinding operations

- Map: one issue labelled `wayfinder:map`, containing Notes,
  Decisions-so-far, and Fog.
- Child tickets: link as GitHub sub-issues. If unavailable, use a task
  list in the map and `Part of #<map>` in each child.
- Ticket labels: `wayfinder:research`, `wayfinder:prototype`,
  `wayfinder:grilling`, or `wayfinder:task`.
- Blocking: use native GitHub issue dependencies. If unavailable,
  record `Blocked by: #<n>` in the child.
- Frontier: first open child in map order with no open blockers
  and no assignee.
- Claim: `gh issue edit <number> --add-assignee @me`.
- Resolve: comment with the answer, close the child, then append
  a summary and link to the map's Decisions-so-far.
