// Format (or check) the markdown tables in this project.
//
// `markdown-table-prettify` ships a stdin -> stdout CLI only, so this wraps its
// API to walk the repo's own markdown files in place.
//
//   node tools/prettify-md.mjs           # rewrite files
//   node tools/prettify-md.mjs --check   # exit 1 if anything would change
//
// Extra args are treated as paths (file or directory) to restrict the walk to.

import { readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { CliPrettify } from "markdown-table-prettify";

const ROOT = resolve(fileURLToPath(import.meta.url), "../..");
const SKIP_DIRS = new Set([
    ".git",
    ".pio",
    ".venv",
    "__pycache__",
    "managed_components",
    "node_modules",
]);

function collect(path) {
    const stats = statSync(path);
    if (stats.isFile()) {
        return path.toLowerCase().endsWith(".md") ? [path] : [];
    }
    return readdirSync(path, { withFileTypes: true }).flatMap((entry) => {
        if (entry.isDirectory()) {
            return SKIP_DIRS.has(entry.name) ? [] : collect(join(path, entry.name));
        }
        return entry.name.toLowerCase().endsWith(".md") ? [join(path, entry.name)] : [];
    });
}

const args = process.argv.slice(2);
const check = args.includes("--check");
const targets = args.filter((arg) => !arg.startsWith("--"));
const files = (targets.length ? targets : [ROOT]).flatMap((target) =>
    collect(resolve(ROOT, target)),
);

const changed = [];
for (const file of files) {
    const before = readFileSync(file, "utf8");
    const after = CliPrettify.prettify(before);
    if (after === before) {
        continue;
    }
    changed.push(relative(ROOT, file));
    if (!check) {
        writeFileSync(file, after);
    }
}

if (!changed.length) {
    console.log(`${files.length} file(s) checked, tables already formatted.`);
    process.exit(0);
}

console.log(`${check ? "Needs formatting" : "Formatted"}:`);
for (const file of changed) {
    console.log(`  ${file}`);
}
process.exit(check ? 1 : 0);
