#pragma once

// FallingStars version - Semantic Versioning 2.0.0 (see https://semver.org/)
//   MAJOR: breaking change to the framework API
//   MINOR: backward-compatible addition (new macro / helper / module hook point)
//   PATCH: backward-compatible fix only
#define FALLINGSTARS_VERSION_MAJOR 0
#define FALLINGSTARS_VERSION_MINOR 1
#define FALLINGSTARS_VERSION_PATCH 0

// Build stamp. The .props file defines FALLINGSTARS_GIT_COMMIT / _GIT_BRANCH
// from MSBuild properties (overridable via /p:GitCommit=... in the build
// scripts). The #ifndef guards below give a sane default when compiling
// outside the MSBuild pipeline.
#ifndef FALLINGSTARS_GIT_COMMIT
#define FALLINGSTARS_GIT_COMMIT "dev"
#endif
#ifndef FALLINGSTARS_GIT_BRANCH
#define FALLINGSTARS_GIT_BRANCH "local"
#endif

// ---------------------------------------------------------------------------
// Feature enable flags. Override from FallingStars.props (PreprocessorDefinitions)
// or directly here. Keep example hooks DISABLED until you supply real addresses
// from IDA/Ghidra - an invalid hook address would crash gamemd.exe at injection.
// ---------------------------------------------------------------------------
#ifndef FALLINGSTARS_ENABLE_EXAMPLE_HOOKS
#define FALLINGSTARS_ENABLE_EXAMPLE_HOOKS 0
#endif

// Example hook address placeholder (TechnoTypeClass::LoadFromINI).
// Replace 0x00000000 with the real gamemd.exe address, then set
// FALLINGSTARS_ENABLE_EXAMPLE_HOOKS=1 to activate the demo hook.
#ifndef FALLINGSTARS_TECHNOTYPE_LOAD_ADDR
#define FALLINGSTARS_TECHNOTYPE_LOAD_ADDR 0x00000000
#endif
