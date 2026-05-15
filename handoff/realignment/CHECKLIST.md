# MCOS Realignment Checklist

## Global gates

- [ ] No direct AI-provider execution remains in MCOS.
- [ ] AI clients connect through gateway/onboarding profiles.
- [ ] native HTTP.sys adapter exists or native gateway adapter exists.
- [ ] DNS-SD/mDNS discovery exists.
- [ ] `/.well-known/mcos.json` exists.
- [ ] Generic onboarding profile exists.
- [ ] Claude Code profile exists.
- [ ] Codex profile exists.
- [ ] Grok profile exists.
- [ ] ChatGPT connector-edge path is documented.
- [ ] CLU/Forsetti governance bundles are delivered.
- [ ] MCP/sub-agent workers are supervised.
- [ ] Autoscaling starts same-type workers under load.
- [ ] Existing stateful sessions drain; new sessions route to new instances.
- [ ] Host CPU/GPU/network/disk telemetry is exposed.
- [ ] Per-client telemetry is honest and heartbeat-based where needed.
- [ ] Dashboard is gateway/pool/client centered.
- [ ] Windows CI gates build/test/package.
- [ ] Release workflow cannot bypass same-SHA Windows gate.

## Phase completion

- [ ] PHASE-00 complete
- [ ] PHASE-01 complete
- [ ] PHASE-02 complete
- [ ] PHASE-03 complete
- [ ] PHASE-04 complete
- [ ] PHASE-05 complete
- [ ] PHASE-06 complete
- [ ] PHASE-07 complete
- [ ] PHASE-08 complete
- [ ] PHASE-09 complete
- [ ] PHASE-10 complete
- [ ] PHASE-11 complete
