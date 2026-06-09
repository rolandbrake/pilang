import PiStatement from "./PiStatement.js";

export default class PiImportStatement extends PiStatement {
  constructor(importToken, pathParts, tail = null, semicolon = null) {
    super(importToken, semicolon || (tail && tail.lastToken) || pathParts[pathParts.length - 1]);
    this._importToken = importToken;
    this._pathParts = pathParts;
    this._tail = tail;
    this._semicolon = semicolon;
  }

  format(indent = 0) {
    let result = this.indent(indent);
    result += this.formatComments(this._importToken, indent, "leading");
    result += "import " + this._pathParts.map((part) => part.value).join(".");
    if (this._tail) {
      if (this._tail.kind === "alias") {
        result += ":" + this._tail.alias.value;
      } else if (this._tail.kind === "wildcard") {
        result += ".*";
      } else if (this._tail.kind === "braced") {
        const bindings = this._tail.bindings.map((binding) => {
          return binding.alias ? `${binding.name.value}:${binding.alias.value}` : binding.name.value;
        });
        result += ".{" + bindings.join(", ") + "}";
      }
    }
    result += ";";
    return result;
  }

  minify(context) {
    let result = "import " + this._pathParts.map((part) => part.value).join(".");
    if (this._tail) {
      if (this._tail.kind === "alias") {
        const local = context.setValue(this._tail.alias.value);
        result += ":" + local;
      } else if (this._tail.kind === "wildcard") {
        result += ".*";
      } else if (this._tail.kind === "braced") {
        result += ".{" + this._tail.bindings.map((binding) => {
          const local = binding.alias || binding.name;
          const mangled = context.setValue(local.value);
          return `${binding.name.value}:${mangled}`;
        }).join(",") + "}";
      }
    } else {
      const localName = this._pathParts[this._pathParts.length - 1].value;
      const mangled = context.setValue(localName);
      if (mangled !== localName) result += ":" + mangled;
    }
    return result + ";";
  }
}
