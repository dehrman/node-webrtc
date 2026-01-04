#!/usr/bin/env node
/* eslint no-console:0, no-process-env:0 */
"use strict";

const os = require("os");
const path = require("path");
const fs = require("fs");
const { spawnSync } = require("child_process");
const { platform, arch, buildFolder } = require("./build-vars.js");

const args = ["-O", buildFolder, "-a", arch];

if (process.env.DEBUG) {
  args.push(...["--debug", "--CDCMAKE_EXPORT_COMPILE_COMMANDS=1"]);
}

if (platform === "win32") {
  args.push(...["-G", "Ninja"]);
}

if (arch !== os.arch()) {
  args.push(
    `--CDCMAKE_TOOLCHAIN_FILE=toolchains/${platform}-${arch}.toolchain`
  );
}

function maybeSelectWindowsRc(env) {
  if (platform !== "win32") {
    return env;
  }

  // If the user already set RC explicitly, respect it.
  if (env.RC && env.RC.toLowerCase().endsWith(".exe") && fs.existsSync(env.RC)) {
    return env;
  }

  // When running under `npm run`, `node_modules/.bin` is added to PATH, which
  // can cause CMake to pick up the npm `rc` binary instead of the Windows SDK
  // resource compiler (`rc.exe`). If we're in a VS dev prompt, use the SDK path.
  const windowsSdkDir = env.WindowsSdkDir || env.WindowsSDKDir;
  const windowsSdkVersion = env.WindowsSDKVersion || env.WindowsSdkVersion;
  if (!windowsSdkDir || !windowsSdkVersion) {
    return env;
  }

  const candidate = path.join(
    windowsSdkDir,
    "bin",
    windowsSdkVersion,
    "x64",
    "rc.exe"
  );

  if (!fs.existsSync(candidate)) {
    return env;
  }

  console.log(`Using Windows SDK rc.exe from ${candidate}`);
  return { ...env, RC: candidate };
}

function main() {
  const env = maybeSelectWindowsRc(process.env);
  console.log("Running cmake-js " + args.join(" "));
  let { status } = spawnSync("cmake-js", ["configure", ...args], {
    shell: true,
    stdio: "inherit",
    env,
  });
  if (status) {
    throw new Error("cmake-js configure failed for wrtc");
  }

  console.log("Running cmake-js build");
  status = spawnSync("cmake-js", ["build", ...args], {
    shell: true,
    stdio: "inherit",
    env,
  }).status;
  if (status) {
    throw new Error("cmake-js build failed for wrtc");
  }

  console.log("Built wrtc");
}

module.exports = main;

if (require.main === module) {
  main();
}
