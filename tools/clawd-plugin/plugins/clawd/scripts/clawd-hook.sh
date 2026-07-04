#!/usr/bin/env bash
# clawd hook — Claude Code olayini clawd cihazina (clawd.local) POST /e ile yollar.
#
# TASARIM (bkz. clawd-device-protocol.md + src/main.cpp mapEvent):
#   - FIRE-AND-FORGET: Claude Code'u ASLA yavaslatma/bloklama. curl arka planda,
#     kisa timeout; her zaman `exit 0`, stdout'a HICBIR SEY yazma (PreToolUse'da
#     yanlis cikti araci bloklayabilir).
#   - SAKIN AMA CANLI: firmware `if (id != curAnim) setAnim(...)` ile ayni animasyona
#     giden olayi yutar -> tool.pre'yi her araca yollasak da titreme olmaz. Titreme
#     yalniz hacking<->idle GIDIP GELMESINDEN cikardi; o yuzden PostToolUse basaride
#     idle GONDERMEYIZ. Bir turun akisi: think -> hacking -> (git'te happy) -> idle.
#
# OLAY ESLEMESI:
#   UserPromptSubmit    -> {k:"prompt.submit"}          -> THINK
#   PreToolUse (Task)   -> {k:"agent.spawn"}            -> AGENTS pozu + altta 4 mini clawd BELIRIR
#   PreToolUse (diger)  -> {k:"tool.pre", d:{g,tool,s}} -> HACKING (tekrar = no-op)
#   SubagentStop        -> {k:"agent.done"}             -> is bitince (sayac 0) 4 mini de GIDER
#   PostToolUse (Bash)  -> git commit/push ise {k:"git"} -> HAPPY (aksi halde sessiz)
#   PostToolUseFailure  -> {k:"tool.post", d:{ok:false}} -> OOPS (guvenilir hata sinyali)
#   PreCompact          -> {k:"compact"}                -> COMPACT (beyin + yanip sonen yildizlar)
#   SessionStart        -> {k:"session.start"}          -> HAPPY
#   Stop                -> {k:"session.stop"}           -> IDLE
#
# Cihaz adresi. VARSAYILAN = clawd.local (mDNS) -> tasinabilir, IP bilmeye gerek
# yok, saglikli aglarda kutudan calisir. mDNS'in yavas/olu oldugu aglarda cihazin
# DOGRUDAN IP'sini ver -> sifir DNS gecikmesi. Nereye? Projenin
# .claude/settings.local.json'una (kisisel, gitignore'lu) env blogu:
#     { "env": { "CLAWD_HOST": "192.168.1.NN" }, "enabledPlugins": {...} }
# Claude Code bu env'i hook'a enjekte eder. En saglami: router'da cihaza DHCP
# rezervasyonu tanimla, IP sabit kalsin. (Detay: bu klasordeki README.md.)
# curl ARKA PLANDA + ciktisi /dev/null oldugundan bash aninda doner; Claude beklemez.

set -u

HOST="${CLAWD_HOST:-clawd.local}"
URL="http://${HOST}/e"
TIMEOUT="${CLAWD_TIMEOUT:-2}"

INPUT="$(cat)"
jqr() { printf '%s' "$INPUT" | jq -r "$1" 2>/dev/null; }

ev="$(jqr '.hook_event_name // empty')"
tool="$(jqr '.tool_name // empty')"

# Claude Code araci -> protokol grubu (d.g). Firmware su an grup'a gore dallanmiyor
# ama protokol normalizasyonu ileri-uyumlu kalsin.
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
    # ALT-AGENT: Task/Agent tool'u = Claude bir alt-agent aciyor. Sıradan tool.pre
    # yerine ozel agent.spawn yolla -> cihazda buyuk maskot AGENTS pozuna gecer
    # (yukari kayip asagi bakar) ve altta 4 mini clawd BIRDEN belirir; is bitince
    # (SubagentStop -> agent.done, sayac 0) hepsi kaybolur.
    # (Tool adi ortama gore "Task" ya da "Agent" olabilir -> ikisini de yakala.)
    if [ "$tool" = "Task" ] || [ "$tool" = "Agent" ]; then
      body='{"k":"agent.spawn"}'
    else
      g="$(group "$tool")"
      # en anlamli tek ozet: komut / dosya / desen / url
      s="$(jqr '[.tool_input.command, .tool_input.file_path, .tool_input.pattern, .tool_input.url]
                | map(select(. != null and . != "")) | (.[0] // "") | tostring')"
      s="${s:0:40}"
      body="$(jq -nc --arg g "$g" --arg tool "$tool" --arg s "$s" '{k:"tool.pre",d:{g:$g,tool:$tool,s:$s}}')"
    fi
    ;;

  PostToolUse)
    # AskUserQuestion/ExitPlanMode CEVAPLANINCA: clawd "?" (ask) pozundan ciksin AMA
    # idle'a DUSMESIN. PostToolUse bu tool bitince (=kullanici cevaplayinca) tetiklenir;
    # cevaptan sonra Claude cevabi isleyip DUSUNMEYE gecer, o yuzden think:on yolla ->
    # firmware ANIM_THINK'e gecer (satir 77) ve tool HUD'unu temizler. Boylece "?"
    # asili kalmaz ve bir sonraki gercek olaya (tool.pre/stop) kadar think'te bekler.
    case "$tool" in
      AskUserQuestion|ExitPlanMode)
        body='{"k":"think","d":{"on":true}}'
        ;;
      *)
        # Yalniz git commit/push basarisi -> kutlama. Diger basarilarda SESSIZ kal
        # (hacking state'i bozulmasin, titreme olmasin).
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
    # Arac HATASI -> oops (facepalm). tool_error.isError bu event'te hep true.
    body='{"k":"tool.post","d":{"ok":false}}'
    ;;

  SubagentStop)  body='{"k":"agent.done"}' ;;   # bir alt-agent bitti -> yan mini "yurur gider"
  PreCompact)    body='{"k":"compact"}' ;;
  SessionStart)  body='{"k":"session.start"}' ;;
  Stop)          body='{"k":"session.stop"}' ;;

  *) exit 0 ;;
esac

[ -z "$body" ] && exit 0

# Fire-and-forget: kisa timeout + arka plan; cihaz erisemese bile Claude takilmaz.
curl -s --max-time "$TIMEOUT" -X POST "$URL" \
  -H 'content-type: application/json' -d "$body" >/dev/null 2>&1 &

exit 0
