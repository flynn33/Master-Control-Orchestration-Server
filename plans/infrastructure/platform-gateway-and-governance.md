## Platform Gateway And Governance Architecture

### Purpose
Master Control Orchestration Server hosts platform-aware gateway and governance lanes inside the Forsetti framework so agents and operators can work through one orchestration surface while still targeting Windows, macOS, and iOS workflows correctly.

### Gateway Model
- gateway modules are Forsetti modules inside the product, not external sidecars
- clients discover the product through platform-specific gateway surfaces
- the current architecture keeps the gateway logic inside the local runtime and service APIs rather than a separate legacy aggregator deployment

### Governance Model
- CLU is the governance coordinator
- platform governance flows are routed by target platform instead of host OS alone
- governance tools execute through framework contracts instead of direct module-to-module shortcuts
- the UI reads governance state through the framework and does not become a second governance engine

### Current Platform Lanes
- Windows gateway and Windows governance lane
- macOS gateway and macOS governance lane
- iOS gateway and iOS governance lane

### Apple Execution Fabric
- Apple hosts can be selected per host using SSH or companion-service transport
- readiness includes Xcode, SDK, simulator, device control, signing, and notarization state
- operations include build, test, archive, export, install, sign, notarize, staple, replay, and history

### Deployment Direction
- Windows remains the primary hosted runtime for the orchestration server
- platform lanes remain module-driven and Forsetti-compliant
- deployment work is currently focused on installer polish, identity alignment, and target-host validation rather than new gateway topology changes
