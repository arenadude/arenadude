#!/usr/bin/env python3
"""Makes a self-contained copy of a built ArenaDude.app that runs on a Mac without Homebrew.

    tools/package_mac.py <built ArenaDude.app> <output .app>

Runs macdeployqt, drops the Qt plugins the tracker doesn't use (and the libraries only they need),
rewrites the absolute Homebrew paths macdeployqt leaves behind, checks that no library is loaded
from outside the bundle, and signs the bundle ad hoc.
"""
import os
import re
import shutil
import subprocess
import sys

KEEP_PLUGINS = {
    "platforms": {"libqcocoa.dylib"},
    "imageformats": {"libqjpeg.dylib", "libqwebp.dylib"},   # PNG is built in
    "tls": {"libqsecuretransportbackend.dylib", "libqcertonlybackend.dylib"},
    "networkinformation": {"libqapplenetworkinformation.dylib"},
    "styles": {"libqmacstyle.dylib"},
}
OUTSIDE = re.compile(r"^(/opt/homebrew|/usr/local)/")


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def is_macho(path):
    with open(path, "rb") as f:
        return f.read(4) in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")


def macho_files(app):
    for root, _, files in os.walk(app):
        for name in files:
            path = os.path.join(root, name)
            if not os.path.islink(path) and is_macho(path):
                yield path


def deps(path):
    """Install name (for libraries) and dependencies of a Mach-O file."""
    lines = run("otool", "-L", path).splitlines()[1:]
    return [line.strip().split(" (")[0] for line in lines]


def rpaths(path):
    """LC_RPATH entries of a Mach-O file."""
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset", run("otool", "-l", path))


def bundled_name(dep):
    """Path of a dependency inside Contents/Frameworks."""
    match = re.search(r"([^/]+\.framework/.*)$", dep)
    return match.group(1) if match else os.path.basename(dep)


def main():
    src, out = sys.argv[1], sys.argv[2]
    if os.path.exists(out):
        shutil.rmtree(out)
    run("ditto", src, out)
    macdeployqt = shutil.which("macdeployqt") or "/opt/homebrew/bin/macdeployqt"
    run(macdeployqt, out)

    contents = os.path.join(out, "Contents")
    frameworks = os.path.join(contents, "Frameworks")
    plugins = os.path.join(contents, "PlugIns")
    for group in os.listdir(plugins):
        group_dir = os.path.join(plugins, group)
        for name in os.listdir(group_dir):
            if name not in KEEP_PLUGINS.get(group, set()):
                os.remove(os.path.join(group_dir, name))
        if not os.listdir(group_dir):
            os.rmdir(group_dir)

    # macdeployqt misses the Homebrew paths that differ from the one it found a library by, and the @rpath
    # references: Homebrew libraries search their rpaths in Homebrew, which loads a second copy of them
    # (two OpenMP runtimes abort the app) on a Mac with Homebrew, and nothing on one without it
    for path in macho_files(out):
        for rpath in rpaths(path):
            if OUTSIDE.match(rpath) or rpath.startswith("@loader_path"):
                run("install_name_tool", "-delete_rpath", rpath, path)
        for dep in deps(path):
            bundled = None
            if OUTSIDE.match(dep):
                bundled = bundled_name(dep)
            elif dep.startswith("@rpath/") and os.path.exists(os.path.join(frameworks, dep[len("@rpath/"):])):
                bundled = dep[len("@rpath/"):]
            if bundled is None:
                continue
            new = "@executable_path/../Frameworks/" + bundled
            if os.path.basename(dep) == os.path.basename(path):
                run("install_name_tool", "-id", new, path)
            else:
                run("install_name_tool", "-change", dep, new, path)

    # Keep only the libraries reachable from the executable and the kept plugins
    def resolve(dep):
        for prefix in ("@executable_path/../Frameworks/", "@rpath/", "@loader_path/../Frameworks/"):
            if dep.startswith(prefix):
                return os.path.join(frameworks, dep[len(prefix):])
        return None

    roots = [p for p in macho_files(out) if not p.startswith(frameworks + os.sep)]
    reachable, todo = set(), list(roots)
    while todo:
        path = todo.pop()
        for dep in deps(path):
            target = resolve(dep)
            if target and os.path.exists(target) and target not in reachable:
                reachable.add(target)
                todo.append(target)
    for name in os.listdir(frameworks):
        item = os.path.join(frameworks, name)
        if name.endswith(".framework"):
            if not any(p.startswith(item + os.sep) for p in reachable):
                shutil.rmtree(item)
        elif os.path.realpath(item) not in {os.path.realpath(p) for p in reachable}:
            os.remove(item)

    leaks = [(p, d) for p in macho_files(out) for d in deps(p) + rpaths(p) if OUTSIDE.match(d)]
    for path, dep in leaks:
        print("Outside the bundle:", os.path.relpath(path, out), "->", dep, file=sys.stderr)
    if leaks:
        sys.exit(1)

    run("codesign", "--force", "--deep", "--sign", "-", out)
    print(out)


if __name__ == "__main__":
    main()
