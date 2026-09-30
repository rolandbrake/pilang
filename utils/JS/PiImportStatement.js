import PiStatement from "./PiStatement.js";

export default class PiImportStatement extends PiStatement {
  constructor(importToken, items, semicolon = null) {
    const lastItem = items[items.length - 1];
    super(importToken, semicolon || lastItem.lastToken);
    this._importToken = importToken;
    this._items = items;
    this._semicolon = semicolon;
  }

  formatItem(item) {
    let result = item.pathParts.map((part) => part.value).join(".");
    if (item.tail) {
      if (item.tail.kind === "alias") {
        result += ":" + item.tail.alias.value;
        if (item.tail.selector) {
          result += this.formatTail(item.tail.selector);
        }
      } else {
        result += this.formatTail(item.tail);
      }
    }
    return result;
  }

  formatTail(tail) {
    if (tail.kind === "wildcard") {
      return ".*";
    }
    if (tail.kind === "braced") {
      const bindings = tail.bindings.map((binding) => {
        return binding.alias ? `${binding.name.value}:${binding.alias.value}` : binding.name.value;
      });
      return ".{" + bindings.join(", ") + "}";
    }
    return "";
  }

  format(indent = 0) {
    let result = this.indent(indent);
    result += this.formatComments(this._importToken, indent, "leading");
    result += "import " + this._items.map((item) => this.formatItem(item)).join(", ");
    result += ";";
    return result;
  }

  minifyTail(tail, context) {
    if (tail.kind === "wildcard") {
      return ".*";
    }
    if (tail.kind === "braced") {
      return ".{" + tail.bindings.map((binding) => {
        const local = binding.alias || binding.name;
        const mangled = context.setValue(local.value);
        return `${binding.name.value}:${mangled}`;
      }).join(",") + "}";
    }
    return "";
  }

  minifyItem(item, context) {
    let result = item.pathParts.map((part) => part.value).join(".");
    if (item.tail) {
      if (item.tail.kind === "alias") {
        const local = context.setValue(item.tail.alias.value);
        result += ":" + local;
        if (item.tail.selector) {
          result += this.minifyTail(item.tail.selector, context);
        }
      } else {
        result += this.minifyTail(item.tail, context);
      }
    } else {
      const localName = item.pathParts[item.pathParts.length - 1].value;
      const mangled = context.setValue(localName);
      if (mangled !== localName) result += ":" + mangled;
    }
    return result;
  }

  minify(context) {
    const result = "import " + this._items.map((item) => this.minifyItem(item, context)).join(",");
    return result + ";";
  }
}
