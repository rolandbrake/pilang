import fs from "node:fs";
import PiFormatter from "./PiFormatter.js";
import PiMinifier from "./PiMinifier.js";

const [, , mode, filename] = process.argv;

if (!["fmt", "min"].includes(mode) || !filename) {
  console.error("Usage: node utils/PiCli.js <fmt|min> <file.pi>");
  process.exit(1);
}

const source = fs.readFileSync(filename, "utf8");

try {
  let output;
  if (mode === "fmt") {
    const result = PiFormatter.format(source);
    if (!result.success) {
      console.error(result.error);
      process.exit(1);
    }
    output = result.code;
  } else {
    const result = PiMinifier.minify(source);
    if (!result.success) {
      console.error(result.error);
      process.exit(1);
    }
    output = result.code;
  }

  if (!output.endsWith("\n")) output += "\n";
  fs.writeFileSync(filename, output, "utf8");
} catch (error) {
  if (error && error.message) {
    console.error(error.message);
  } else {
    console.error(String(error));
  }
  process.exit(1);
}
