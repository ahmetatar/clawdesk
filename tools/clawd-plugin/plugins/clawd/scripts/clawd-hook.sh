#!/usr/bin/env bash
# clawd hook — sends the Claude Code event to the clawd device (clawd.local) via POST /e.
#
# DESIGN (see clawd-device-protocol.md + src/main.cpp mapEvent):
#   - FIRE-AND-FORGET: NEVER slow down/block Claude Code. curl in the background,
#     short timeout; always `exit 0`, write NOTHING to stdout (in PreToolUse a
#     wrong output can block the tool).
#   - CALM BUT ALIVE: firmware swallows an event that leads to the same animation via
#     `if (id != curAnim) setAnim(...)` -> even if we send tool.pre for every tool there
#     is no flicker. Flicker only came from GOING BACK AND FORTH hacking<->idle; that is
#     why we DON'T SEND idle on PostToolUse success. A turn's flow: think -> hacking ->
#     (happy on git) -> idle.
#
# EVENT MAPPING:
#   UserPromptSubmit    -> {k:"prompt.submit"}          -> THINK
#   PreToolUse (Task)   -> {k:"agent.spawn"}            -> AGENTS pose + 4 mini clawds APPEAR at the bottom
#   PreToolUse (other)  -> {k:"tool.pre", d:{g,tool,s}} -> HACKING (repeat = no-op)
#   SubagentStop        -> {k:"agent.done"}             -> when work is done (counter 0) all 4 minis LEAVE
#   PostToolUse (Bash)  -> if git commit/push then {k:"git"} -> HAPPY (otherwise silent)
#   PostToolUseFailure  -> {k:"tool.post", d:{ok:false}} -> OOPS (reliable error signal)
#   PreCompact          -> {k:"compact"}                -> COMPACT (brain + blinking stars)
#   SessionStart        -> {k:"session.start"}          -> HAPPY
#   Stop                -> {k:"session.stop"}           -> IDLE
#
# Device address. DEFAULT = clawd.local (mDNS) -> portable, no need to know the IP,
# works out of the box on healthy networks. On networks where mDNS is slow/dead, give
# the device's DIRECT IP -> zero DNS delay. Where? In the project's
# .claude/settings.local.json (personal, gitignored) env block:
#     { "env": { "CLAWD_HOST": "192.168.1.NN" }, "enabledPlugins": {...} }
# Claude Code injects this env into the hook. Most robust: define a DHCP reservation
# for the device on the router so the IP stays fixed. (Details: README.md in this folder.)
# Since curl runs IN THE BACKGROUND + output goes to /dev/null, bash returns instantly; Claude does not wait.

set -u

HOST="${CLAWD_HOST:-clawd.local}"
URL="http://${HOST}/e"
TIMEOUT="${CLAWD_TIMEOUT:-2}"

INPUT="$(cat)"
jqr() { printf '%s' "$INPUT" | jq -r "$1" 2>/dev/null; }

ev="$(jqr '.hook_event_name // empty')"
tool="$(jqr '.tool_name // empty')"

# Claude Code tool -> protocol group (d.g). The firmware does not currently branch on
# group, but keep the protocol normalization forward-compatible.
group() {
  case "$1" in
    Bash)                               echo exec ;;
    Edit|Write|MultiEdit|NotebookEdit)  echo edit ;;
    Read)                               echo read ;;
    Grep|Glob)                          echo search ;;
    WebFetch|WebSearch)                 echo web ;;
    Task|Agent)                         echo agent ;;
    TodoWrite|TaskCreate|TaskUpdate)    echo plan ;;
    mcp__*)                             echo ext ;;
    *)                                  echo "" ;;
  esac
}

body=""
case "$ev" in
  UserPromptSubmit)
    len="$(jqr '(.prompt // "") | length')"
    body="$(jq -nc --argjson len "${len:-0}" '{k:"prompt.submit",d:{len:$len}}')"
    ;;

  PreToolUse)
    # SUB-AGENT: Task/Agent tool = Claude is spawning a sub-agent. Instead of an ordinary
    # tool.pre send a special agent.spawn -> on the device the big mascot switches to the
    # AGENTS pose (slides up and looks down) and 4 mini clawds APPEAR AT ONCE at the bottom;
    # when the work is done (SubagentStop -> agent.done, counter 0) they all disappear.
    # (The tool name may be "Task" or "Agent" depending on the environment -> catch both.)
    if [ "$tool" = "Task" ] || [ "$tool" = "Agent" ]; then
      body='{"k":"agent.spawn"}'
    else
      g="$(group "$tool")"
      # the single most meaningful summary: command / file / pattern / url
      s="$(jqr '[.tool_input.command, .tool_input.file_path, .tool_input.pattern, .tool_input.url]
                | map(select(. != null and . != "")) | (.[0] // "") | tostring')"
      s="${s:0:40}"
      body="$(jq -nc --arg g "$g" --arg tool "$tool" --arg s "$s" '{k:"tool.pre",d:{g:$g,tool:$tool,s:$s}}')"
    fi
    ;;

  PostToolUse)
    # When AskUserQuestion/ExitPlanMode IS ANSWERED: clawd should leave the "?" (ask) pose
    # BUT NOT FALL to idle. PostToolUse fires when this tool finishes (= when the user
    # answers); after the answer Claude processes it and MOVES TO THINKING, so send think:on
    # -> firmware switches to ANIM_THINK (line 77) and clears the tool HUD. This way the "?"
    # does not stay stuck and it waits in think until the next real event (tool.pre/stop).
    case "$tool" in
      AskUserQuestion|ExitPlanMode)
        body='{"k":"think","d":{"on":true}}'
        ;;
      *)
        # Only git commit/push success -> celebrate. Stay SILENT on other successes
        # (don't disrupt the hacking state, no flicker).
        cmd="$(jqr '.tool_input.command // ""')"
        op=""
        case "$cmd" in
          *"git commit"*|*"git ci"*) op="commit" ;;
          *"git push"*)              op="push" ;;
        esac
        [ -z "$op" ] && exit 0
        body="$(jq -nc --arg op "$op" '{k:"git",d:{op:$op}}')"
        ;;
    esac
    ;;

  PostToolUseFailure)
    # Tool ERROR -> oops (facepalm). tool_error.isError is always true in this event.
    body='{"k":"tool.post","d":{"ok":false}}'
    ;;

  SubagentStop)  body='{"k":"agent.done"}' ;;   # a sub-agent finished -> a side mini "walks away"
  PreCompact)    body='{"k":"compact"}' ;;
  SessionStart)  body='{"k":"session.start"}' ;;
  Stop)          body='{"k":"session.stop"}' ;;
  # Terminal closed / session ended: Stop does NOT always fire (e.g. the window is closed
  # while a turn is still running). Without it the device's "Claude is busy" flag would
  # stay set and the screen would not dim/sleep. SessionEnd is the reliable release.
  SessionEnd)    body='{"k":"session.stop"}' ;;

  *) exit 0 ;;
esac

[ -z "$body" ] && exit 0

# Fire-and-forget: short timeout + background; Claude won't hang even if the device is unreachable.
curl -s --max-time "$TIMEOUT" -X POST "$URL" \
  -H 'content-type: application/json' -d "$body" >/dev/null 2>&1 &

exit 0
